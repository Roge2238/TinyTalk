#include "server.h"
#include "timeout_handle.h"
#include "game_server.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>



#define MAX_EPOLL_EVENT 10 // 待改
#define HEAD_LEN 5

// 未登录连接登录时限:10s 内未完成登录即回收(防半连接堆积)
// 注意:客户端心跳上线前,已登录用户不做空闲踢(见 DESIGN.md §7)
#define LOGIN_TIME_OUT_MS 10000

//global 

int epfd = -1;
int timer_fd = -1;

// 连接表:网络层所有连接的登记处

//sid -> Session
std::unordered_map<session_id, std::shared_ptr<Session>> conn_table;
std::atomic<session_id> next_sid{1};

// 账号注册表 uid -> 发送权柄SenFn
Account_table account_table;






// epoll_event.data 是 union ！！！
//使用哨兵地址变量
static int s_tag_listen; // listen_fd
static int s_tag_timer;  // timer_fd

void connect_thread(int listen_fd)
{
    epfd = epoll_create1(0);
    if (epfd < 0) error_die("epoll_create");

    // 超时检测用的定时器
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd < 0) error_die("timerfd_create");

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &s_tag_listen;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    // 定时器:周期 1s 的闹钟 —— 超时模块按 tick 扫描 conn_table
    ev.events = EPOLLIN;
    ev.data.ptr = &s_tag_timer;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);

    struct itimerspec its;
    its.it_interval.tv_sec = 1;
    its.it_interval.tv_nsec = 0;
    its.it_value = its.it_interval;   // 立即开始,之后每秒一响
    if (timerfd_settime(timer_fd, 0, &its, nullptr) < 0)
        error_die("timerfd_settime");

    struct epoll_event events[MAX_EPOLL_EVENT];

    while (1)
    {
        int nfds = epoll_wait(epfd, events, MAX_EPOLL_EVENT, -1);
        if (nfds < 0)
        {
            if (errno == EINTR) continue;
            error_die("epoll_wait");
        }

        // 本批要回收的连接。刻意不在循环中间 teardown:同一批事件里可能还有指向该
        // Session 的裸指针(如 EPOLLIN 与 EPOLLHUP 同时到达、登录踢掉旧连接),
        // 统一留到批处理结束后再执行,避免 use-after-free。
        std::vector<session_id> closing;

        for (int i = 0; i < nfds; i++)
        {
            void* ptr = events[i].data.ptr;
            uint32_t pre_event = events[i].events;

            if (ptr == &s_tag_timer)   // 定时器:超时扫描
            {
                std::vector<session_id> expired = handle_timeout();
                closing.insert(closing.end(), expired.begin(), expired.end());
                continue;
            }
            if (ptr == &s_tag_listen)   // 监听套接字
            {
                if (!(pre_event & EPOLLIN)) continue;

                while (1)   // ET
                {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd = accept4(listen_fd, (struct sockaddr*)&client_addr,
                                            &addr_len, SOCK_NONBLOCK);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR)
                            continue;
                        perror("accept failed");
                        break;
                    }

                    // 新连接:分配全局唯一 sid,并立刻登记进连接表。
                    // Session 从这一刻起由 conn_table 的 shared_ptr 拥有 —— sendFn 里的
                    // weak 也是从这里拷贝的;没有 shared 拥有者的 Session,weak 永远是空。
                    auto sn = std::make_shared<Session>(client_fd, next_sid.fetch_add(1));
                    // 未登录连接给一个"登录时限":超时模块据此回收半连接
                    sn->deadline_ms = get_now_ms() + LOGIN_TIME_OUT_MS;
                    conn_table.emplace(sn->sid, sn);
                    epoll_add(epfd, client_fd, EPOLLIN | EPOLLET, sn.get());
                    printf("新连接: sid=%llu\n", (unsigned long long)sn->sid);
                }
                continue;
            }

            // ---- 业务连接事件 ----
            Session* sn = (Session*)ptr;   
            bool need_close = false;

            if (pre_event & EPOLLIN)
            {
                if (read_msg(sn) < 0)
                {
                    need_close = true;      // 对端关闭 / 读错误
                }
                else
                {
                    // 收到任何数据都刷新登录时限(未登录连接)
                    if (sn->state == STATE_LOGIN)
                        sn->deadline_ms = get_now_ms() + LOGIN_TIME_OUT_MS;

                    handler(sn, closing);  
                }
            }
            if (!need_close && (pre_event & EPOLLOUT))
            {
                if (write_msg(sn) < 0)
                    need_close = true;      // 写失败,连接已不可用
            }
            if ((pre_event & (EPOLLHUP | EPOLLERR)) || need_close)
            {
                closing.push_back(sn->sid);
            }
        }

        //统一回收本批要关闭的连接
        for (session_id sid : closing)
            teardown_session(sid);
    }
}


// ============================================================
static sendFn make_send_fn(const std::shared_ptr<Session>& s)
{
    std::weak_ptr<Session> weak = s;
    return [weak](const Packet& pkg)
    {
        auto sp = weak.lock();
        if (!sp || sp->closed.load())
            return;                        // 连接已不在:静默丢弃
        append_pkg(sp.get(), (char)pkg.type, pkg.body, pkg.body_len);
    };
}




int read_msg(Session* sn)
{
    // 从内核缓冲区读取消息到用户态读缓冲区(ET 模式:读到 EAGAIN 为止)
    char* buf = sn->read_buf + sn->read_pos;
    int left = MAX_BUF - sn->read_pos;

    while (1)
    {
        int n = recv(sn->fd, buf, left, 0);
        if (n > 0)
        {
            sn->read_pos += n;
            buf += n;
            left -= n;
            if (left <= 0) break;   // 读缓冲已满,剩余数据等下次 EPOLLIN
        }
        else if (n == 0)
        {
            return -1;              // 对端关闭
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;              // 本次可读数据已读完
            return -1;              // 真错误
        }
    }
    return 0;
}




int write_msg(Session* sn)
{

    std::lock_guard<std::mutex> lk(sn->write_mtx);
    int sent = 0;
    int total = sn->write_pos; 
    int fd = sn->fd;
    if (total == 0) {
        // 确保不会不必要地监听EPOLLOUT
        epoll_mod(epfd, fd, EPOLLIN | EPOLLET, sn);
        return 0;
    }

    while(sent < total)
    {
        int n = send(fd, sn->write_buf + sent, total - sent,  MSG_NOSIGNAL);
        if(n > 0)
        {
        sent += n;
        }
        else
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK)
            break;
            return -1;
        }
    }
    if(sent == total)
    {
        memset(sn->write_buf, 0, MAX_BUF);
        sn->write_pos = 0;
        epoll_mod(epfd, fd, EPOLLIN | EPOLLET, sn);
    }
    else
    {
        int remain = total - sent;
        memmove(sn->write_buf, sn->write_buf + sent, remain);
        sn->write_pos = remain;
        memset(sn->write_buf + sn->write_pos, 0, MAX_BUF - sn->write_pos);

    }
    return 0;
}


//包装协议 放入写缓冲区
void append_pkg(Session* sn, char type, const char* msg, int len)
{
    std::lock_guard<std::mutex> lk(sn->write_mtx);
   
    if (sn->closed.load())
        return;
    if (len < 0 || msg == nullptr)
        return;
    if (len + HEAD_LEN > MAX_BUF - sn->write_pos) {
        fprintf(stderr, "[append_pkg] BUFFER FULL! Dropping.\n");
        return;
    }
    char head[HEAD_LEN];
    head[0] = type;
    head[1] = (len >> 24) & 0xFF;
    head[2] = (len >> 16) & 0xFF;
    head[3] = (len >> 8) & 0xFF;
    head[4] = (len)     & 0xFF;
    memcpy(sn -> write_buf + sn->write_pos, head, HEAD_LEN);
    sn->write_pos += HEAD_LEN; 
    memcpy(sn -> write_buf + sn->write_pos, msg, len);
    sn->write_pos += len;
    if(epoll_mod(epfd, sn->fd, EPOLLIN | EPOLLOUT | EPOLLET, sn) < 0)
        perror("[append_pkg] epoll_mod failed");
}






// 添加监听对象
int epoll_add(int epfd, int fd, uint32_t event, Session* sn)
{
    struct epoll_event ev;
    ev.events = event;
    ev.data.ptr = sn;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

}



// 修改
int epoll_mod(int epfd, int fd, uint32_t event, Session* sn)
{
    struct epoll_event ev;
    ev.events = event;
    ev.data.ptr = sn;
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}



// epoll删除
int epoll_del(int epfd, int fd)
{
    return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}




void error_die(const char* msg)
{
    perror(msg);
    exit(1);
}




// 服务器启动
int startup(u_short* port)  
{

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    struct sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);  
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int on = 1;
    if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        error_die("setsockopt");  
    }
    
    if (bind(listen_fd, (struct sockaddr*)&name, sizeof(name)) < 0) {
        error_die("bind");
    }
    
    if (*port == 0) {
        socklen_t namelen = sizeof(name);
        if (getsockname(listen_fd, (struct sockaddr*)&name, &namelen) < 0)
        {
            error_die("getsockname");  
        }
        *port = ntohs(name.sin_port);  
    }
    
    if (listen(listen_fd, 5) < 0) {
        error_die("listen");
    }
    
    return listen_fd;
}




// 现阶段还是 if/else;将来模块多了以后会改成"模块注册 type -> 处理器"的注册表
// (见 DESIGN.md 8.2),加新功能时不需要再改这个函数。
void type_handler(Session* sn, char type, char* body, int body_len,
                  std::vector<session_id>& closing)
{
    (void)body_len;   // TODO(R3 聊天模块):body 解析一律按 body_len 做边界检查,
                      // 禁止在裸缓冲上 strcpy / 无界扫描(现在的 type 2 解析就是隐患)

    // ---------- 未登录:只放行 type 1(上线) ----------
    if (sn->state == STATE_LOGIN)
    {
        if (type == 1)
        {
            char user[USER_ID_LEN] = {0};
            strncpy(user, body, USER_ID_LEN - 1);
            user[USER_ID_LEN - 1] = '\0';

            //防止同批事件里还残留指向旧 Session 的裸指针。
            if (auto old_sid = account_table.get_sid(user))
            {
                if (*old_sid != sn->sid)
                {
                    printf("用户 %s 重复登录,旧连接(sid=%llu)将被回收\n",
                           user, (unsigned long long)*old_sid);
                    closing.push_back(*old_sid);
                }
            }

            sn->user_id = user;
            sn->state = STATE_NORMAL;

           
            auto it = conn_table.find(sn->sid);
            if (it == conn_table.end())
            {
                // 防御:理论上到不了这里;真到了说明生命周期管理有 bug,留日志。
                printf("!! 登录时连接表里找不到本连接 sid=%llu\n", (unsigned long long)sn->sid);
                return;
            }
            //user 与 sendfn绑定 
            account_table.bind(user, sn->sid, make_send_fn(it->second));
            printf("用户 %s 已上线\n", user);

            // TODO(R3 聊天模块):上线后补发离线收件箱 Inbox_send(sn)
        }
        return;
    }

    // ---------- 已登录(STATE_NORMAL) ----------
    if (type == 3)   // 查询在线列表
    {
        // 注册表的键集合 == 当前在线用户(登录 bind / 下线 unbind 收口在注册表)
        std::vector<uid> uids = account_table.list_uids();
        char msg[MAX_BUF] = {0};
        if (uids.empty())
        {
            strcpy(msg, "没有人在线喵~ 空悲切 ");
        }
        else
        {
            char tmp[512] = "在线用户有 :";
            int p = strlen(tmp);
            int remaining = sizeof(tmp) - p;
            for (const auto& id : uids)
            {
                int w = snprintf(tmp + p, remaining, " %s", id.c_str());
                if (w <= 0 || w >= remaining) break;
                p += w;
                remaining -= w;
            }
            strncpy(msg, tmp, sizeof(msg) - 1);
        }
        append_pkg(sn, 3, msg, strlen(msg));
    }
    else if (type == 2)
    {
        // TODO(R3 聊天模块):私聊转发。原版语义 body = "目标id?发送者id: 内容";
        // 现在用裸指针找 '?' 后无界拷贝,是隐患;需改为按 body_len 解析,并接回
        // Inbox_add(离线存档)与 notify_user(在线通知) —— 随收件箱模块一起搬入。
    }
    else if (type == 4 || type == 5)
    {
        // TODO(R4 游戏模块):type 4 = 加入匹配,type 5 = 游戏内数据(出拳)。
        // 设计见 DESIGN.md 9:匹配队列只存 uid、对局线程持 shared_ptr 保活,
        // 玩家消息全部通过注册表拷出的 sendFn 发送,不碰 Session。
    }
    // 其他 type 静默忽略(协议错误处理策略后续轮次再定)
}
void handler(Session* sn, std::vector<session_id>& closing)
{
    // 处理client的消息

    //从sn 的用户态缓冲区读取 处理 
    char* buf = sn->read_buf;
    int len = sn->read_pos;
    int pos = 0;

    while(pos + HEAD_LEN <= len)
    {
        int type = buf[pos];
        int body_len = (buf[pos + 1] << 24) | (buf[pos + 2] << 16) | (buf[pos + 3] << 8) | buf[pos + 4];
        pos += HEAD_LEN;
        if(body_len < 0 || body_len > MAX_BUF)
        {
            fprintf(stderr, "[handler] Invalid body_len: %d\n", body_len);
            sn->read_pos = 0;
            memset(sn->read_buf, 0, MAX_BUF);
            return;
        }

        if(pos + body_len > len)
        {
            pos -= HEAD_LEN;
            break;
        }
        char* body = buf + pos;
        type_handler(sn, type, body, body_len, closing);

        pos += body_len;
    }

    if(pos > 0 && pos < len)
    {
        memmove(sn ->read_buf, sn->read_buf + pos, len  - pos);
        sn->read_pos = len - pos;
        memset(sn->read_buf + sn->read_pos, 0, MAX_BUF - sn->read_pos);
    }

    if( pos >= len)
    {
        sn->read_pos = 0;
        memset(sn ->read_buf, 0, MAX_BUF);
    }
   

}


// ============================================================
// 连接资源统一回收出口(设计原则:生命周期收口)
// 所有"这个连接没了"的路径 —— 对端关闭 / 读错误 / 写错误 / EPOLLHUP / 超时 / 重复登录踢人 ——
// 都必须汇聚到这里,不允许散落在各处手动 delete。
// 只允许 io 线程调用;同一批 epoll 事件处理完后再执行(见 connect_thread 的 closing 列表)。
//
// 顺序固定,不要调换:
//   1) epoll 摘除  —— 之后该 fd 不会再产生任何事件
//   2) closed 置位 —— 其他线程若正持有 shared_ptr 在发送,看到 closed 后立即放弃
//   3) 连接表摘除  —— conn_table 是 Session 的唯一长期拥有者;若无在途引用,对象在此析构
//   4) 注册表摘除  —— 用户下线(带 sid 校验,旧连接回收不会误删同名新登录)
//   5) close(fd)   —— 最后关 fd:即使有在途发送者,也只能写进将死的对象,不会碰到 fd
// ============================================================
void teardown_session(session_id sid)
{
    auto it = conn_table.find(sid);
    if (it == conn_table.end())
        return;   // 已回收过 同批事件里可能被记了多次

    auto sn = it->second;

    epoll_del(epfd, sn->fd);              // 1
    sn->closed.store(true);               // 2
    conn_table.erase(it);                 // 3 (sn 由本函数局部引用继续持有,安全)

    if (!sn->user_id.empty())// 用户已登录
    {
        account_table.unbind(sn->user_id, sid);   // 4

        // 4.5 通知各模块"用户下线":现阶段直接调游戏模块的回调
        On_user_offline(sn->user_id);
    }
    close(sn->fd);                        // 5

    printf("连接已回收: sid=%llu user=%s\n",
           (unsigned long long)sid,
           sn->user_id.empty() ? "(未登录)" : sn->user_id.c_str());
}



// 先查注册表拿到 sid,再走统一的 teardown_session
void free_resource(uid user_id, int epfd)
{
    (void)epfd;   // 暂未使用;超时模块重写(改扫连接表)时会连同签名一起清理
    // 下线通知已收口在 teardown_session(所有下线路径的唯一出口),这里不重复调用
    if (auto s = account_table.get_sid(user_id))
        teardown_session(*s);
}
