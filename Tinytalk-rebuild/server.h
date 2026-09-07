#pragma once
// ============================================================
// server.h —— 网络层(现阶段网络层与应用层还混在 server.cpp 里,
// 本文件先承载网络层对象定义与内部接口声明;后续轮次再拆成 net/ 目录)
//
// 本轮(R1)解决的问题:Session 所有权模型。
//   教训:Session 曾经继承 enable_shared_from_this 却用裸 new/delete 管理,
//   weak_from_this() 得到的永远是空 weak_ptr —— sendFn 闭包 lock 永远失败,
//   所有外发消息被静默丢弃。现在 weak 直接从 conn_table 的 shared_ptr 拷贝,
//   不再需要继承 enable_shared_from_this。
// ============================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <sys/types.h>
#include <sys/timerfd.h>

// ---- 协议常量(继承自原 common.h;将来并入 protocol.h 让客户端共用)----
#define MAX_BUF 1024       // 单帧 body 上限(读/写缓冲都是这个大小)
#define USER_ID_LEN 10     // 用户 id 定长,不足部分按 '\0' 补齐
#define HEAD_LEN 5         // 帧头长度:1 字节 type + 4 字节大端 body 长度

// 消息类型:1=用户上线 2=私聊 3=查询在线列表 4=游戏(匹配/匹配成功) 5=游戏(出拳/结果)

// 一帧消息(解析后、发送前的统一形态)。
// 注意:body 现阶段是发送方提供的裸指针,只保证"调用期间"有效 —— 目前所有发送
// 都发生在 io 线程内同步完成,所以安全;将来模块要异步发送时,必须改成自持有的
// std::vector<char>(TODO M3)。
struct Packet
{
    uint8_t type;
    uint32_t body_len;
    char* body;
};

// 发送权柄:上层模块(游戏、收件箱……)拿到手的唯一"给这个用户发消息"的方式。
// 内部持有 Session 的 weak_ptr,连接断开后自动失效,发送变成空操作 —— 杜绝悬垂指针。
using sendFn = std::function<void(const Packet&)>;
using uid = std::string;
using session_id = uint64_t;   // 连接唯一 id,accept 时从自增计数器分配,之后不再变

// 注册表里的一项:一个在线用户对应"他当前这条连接"的发送权柄
struct AccountEntry
{
    session_id sid;
    sendFn fn;
};

enum state_t
{
    STATE_INIT = 0,   // 保留未用
    STATE_LOGIN,      // 已建立 TCP、等待登录包(新连接的初始状态)
    STATE_NORMAL,     // 已登录,可收发业务消息
    STATE_GAME,       // 保留未用:游戏状态属于游戏模块,网络层不感知,后续轮次清理
    STATE_CLOSED
};

// ------------------------------------------------------------
// Session —— 一次 TCP 连接(网络层对象)
//
// 所有权模型:由 server.cpp 的 conn_table(shared_ptr)唯一长期拥有。
//   上层模块拿不到 Session 指针,只能拿到 sendFn(内部持有本对象的 weak_ptr);
//   epoll 事件等内部使用的 Session* 都是非拥有的裸指针,只在 io 线程内、
//   Session 存活期间使用,绝不允许跨线程保存。
// ------------------------------------------------------------
class Session
{
public:
    int fd = -1;
    uid user_id;                  // 登录前为空字符串
    session_id sid;               // accept 时分配,全局唯一
    state_t state;

    // 读缓冲区:io 线程专用(拆帧时可能越界扫描,所以必须清零,见构造函数)
    char read_buf[MAX_BUF];
    int read_pos;
    // 写缓冲区:append_pkg / write_msg 可能被模块线程调用,用 write_mtx 保护
    char write_buf[MAX_BUF];
    int write_pos;
    std::mutex write_mtx;

    // teardown 流程置位;置位后发送方不得再触碰本对象的 fd / 缓冲(见 append_pkg)
    std::atomic<bool> closed{false};
    uint64_t deadline_ms;         // 最后活跃时间(超时模块轮次启用)

    explicit Session(int fd, session_id sid)
        : fd(fd), sid(sid), state(STATE_LOGIN), read_pos(0), write_pos(0), deadline_ms(0)
    {
        // 血泪教训:缓冲必须清零。解析代码会在读缓冲里做无界扫描,
        // 未初始化的内存会让同样的输入产生不可复现的行为。
        memset(read_buf, 0, sizeof(read_buf));
        memset(write_buf, 0, sizeof(write_buf));
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

// ------------------------------------------------------------
// Account_table —— 账号注册表(uid -> 发送权柄)
//   应用层判断"谁在线"的唯一依据:登录时 bind,下线时 unbind。
//   线程安全:内部自带锁,调用方不要在外面再套锁(曾经在外部另加一把
//   account_table_mtx"双保险",反而造成锁顺序混乱,已移除)。
// ------------------------------------------------------------
class Account_table
{
public:
    // 注册(登录成功时调用;同 uid 重复登录会覆盖旧条目 —— 旧连接的回收带
    // sid 校验,不会误删新条目)
    void bind(const uid& user_id, session_id sid, const sendFn& fn)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        slot_map_[user_id] = AccountEntry{sid, fn};
    }

    // 注销(下线时调用)。只有 sid 匹配才删除:
    // 防止"旧连接晚到的回收"误删同名用户的新登录。
    void unbind(const uid& user_id, session_id sid)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = slot_map_.find(user_id);
        if (it != slot_map_.end() && it->second.sid == sid)
            slot_map_.erase(it);
    }

    std::optional<sendFn> get_send_fn(const uid& user_id)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = slot_map_.find(user_id);
        if (it != slot_map_.end()) return it->second.fn;
        return std::nullopt;
    }

    std::optional<session_id> get_sid(const uid& user_id)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = slot_map_.find(user_id);
        if (it != slot_map_.end()) return it->second.sid;
        return std::nullopt;
    }

    // 在线列表:注册表的键集合就是"当前已登录用户"
    std::vector<uid> list_uids()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<uid> out;
        out.reserve(slot_map_.size());
        for (const auto& kv : slot_map_) out.push_back(kv.first);
        return out;
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<uid, AccountEntry> slot_map_;
};

// ---- 网络层内部接口(实现都在 server.cpp)----
// 声明放在这里是为了解决"先调用、后定义"的编译顺序问题;
// 将来网络层拆成独立文件(net/)时,这些声明会原样搬走。
void error_die(const char* msg);
int startup(u_short* port);          // 建监听套接字
void connect_thread(int listen_fd);  // io 线程主循环(唯一 epoll_wait 的地方)
int  epoll_add(int epfd, int fd, uint32_t event, Session* sn);
int  epoll_mod(int epfd, int fd, uint32_t event, Session* sn);
int  epoll_del(int epfd, int fd);
int  read_msg(Session* sn);          // 收包 -> 读缓冲
int  write_msg(Session* sn);         // 写缓冲 -> 内核(EPOLLOUT 时)
void append_pkg(Session* sn, char type, const char* msg, int len);  // 打包一帧进写缓冲
void type_handler(Session* sn, char type, char* body, int body_len,
                  std::vector<session_id>& closing);   // 按状态 + 类型分发
void handler(Session* sn, std::vector<session_id>& closing);  // 拆帧主循环
void teardown_session(session_id sid);    // 连接资源统一回收出口
void free_resource(uid user_id, int epfd);  // uid 版回收入口(超时模块在用)
