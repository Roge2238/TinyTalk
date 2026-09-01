#ifndef COMMON_H
#define COMMON_H
#include <cstdio>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <sys/epoll.h>


// 将网络层的 消息收发结构体独立 用Session 管理  其他 线程通过weak_ptr 临时使用  
// send_slot_map 维护一个user_id 和 send_fn 的映射表  通过user_id 找到对应的send_fn 发送消息
// 每一个功能模块 独立结构体 利用session


/// 消息 ： packet



struct packet
{
    uint8_t type;
    uint32_t body_len;
    char* body;
};


using sendFn = std::function<void(const packet&)>;
using uid = std::string;

struct AccountEntry
{
    Id session_id;
    sendFn fn;
};


enum state_t
{
    STATE_INIT = 0,
    STATE_LOGIN,
    STATE_GAME,
    STATE_CLOSED
};



class Session : public std::enable_shared_from_this<Session>
{
    using Ptr = std::shared_ptr<Session>;
    using WeakPtr = std::weak_ptr<Session>;
    using Id = uint64_t;

    public:
        int fd;
        Id id;
        state_t state;

        //读缓冲区
        char read_buf[MAX_BUF];
        int read_pos;
        std::mutex read_mtx;
        //写缓冲区
        char write_buf[MAX_BUF];
        int write_pos;  
        std::mutex write_mtx;

        explicit Session(int fd, Id user_id) :
            fd(fd), id(user_id), state(STATE_INIT) {}
        
        
        WeakPtr get_weak_ptr(){return weak_from_this();}

        //禁止拷贝构造和赋值运算
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;


}



// 内部拥有 user 的 id 和对应 SendFn的映射 哈希表
class Account_table
{
    std::unordered_map<uid, AccountEntry> send_slot_map;

    void bind_send_fn(uid, session_id, sendFn fn)
    {
        send_slot_map[uid] = {session_id , fn};
    }


    std::optional<sendFn> get_send_fn(uid)
    {
        auto it = send_slot_map.find(uid);
        if(it != send_slot_map.end())
        {
            return it->second.fn;
        }
        return std::nullopt;
    }
    // 加入 删除 send_fn 的逻辑

    void delete_send_fn(uid)
    {
        send_slot_map.erase(uid);
    }

}



void on_login(Session& session, uid& user_id)
{
    auto weak = session.get_weak_ptr();
    // 捕获weak 获得 session的 消息槽    消息槽: 发送消息的权柄
    sendFn out = [weak](const packet& pkg)
    {
        if(auto s = weak.lock())
        {
            // 发送消息的逻辑 
        }
    }
    // 将user_id 和 send_Fn 绑定
    send_slot_map.bind_send_fn(user_id, session.id, out);
    

}