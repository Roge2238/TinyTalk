#include "server.h"
#include "game_server.h"
#include "timeout_handle.h"



#define MAX_EPOLL_EVENT 10 // 待改
#define HEAD_LEN 5

//断连检测 10s 
#define CLIENT_TIME_OUT_MS 10000

//global 

int epfd = -1;
int timer_fd = -1;

std:: unordered_map<std::string, Session*> online_table;
std::mutex online_table_mtx;
Account_table account_table;
std::mutex account_table_mtx;

extern game_manager;






void connect_thread(int listen_fd)
{

    epfd = epoll_create1(0);

    //启用超时检测闹钟 time_fd;
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);



    if(epfd < 0) error_doe("epoll_create");
    
    //开始只有listen_fd 原始监听

    epoll_add(epfd, listen_fd, EPOLLIN | EPOLLET, nullptr);

    epoll_add(epfd, timer_fd, EPOLLIN, nullptr);
    //储存监听事件数组
    struct epoll_event events[MAX_EPOLL_EVENT];

    while(1)
    {
        //epoll_wait 操作
        int nfds = epoll_wait(epfd, events, MAX_EPOLL_EVENT, -1);
        if(epfd < 0) error_die("epoll_wait");

        for(int i= 0; i < nfds; i++)
        {
            // 处理逻辑
            void* ptr = events[i].data.ptr;
            uint32_t pre_event = events[i].events;

            int fd = events[i].data.fd;

            if(fd == timer_fd)
            {
                //闹钟响
                handle_timeout();
            }


            if(fd == listen_fd && ptr == nullptr)
            {
                if (!(pre_event & EPOLLIN)) continue;
            
                while(1)
                {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);

                    int client_fd = accept4(listen_fd, (struct sockaddr*)&client_addr, &addr_len, SOCK_NONBLOCK);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR)
                            continue;
                        perror("accept failed");
            
                    }
                    printf("新连接: %s\n", inet_ntoa(client_addr.sin_addr));
                    
                    Session* sn = new Session();
                    sn->fd = client_fd;

                    epoll_add(epfd, client_fd, EPOLLIN | EPOLLET, sn);
                }
            }
            else
            {
                Session* sn = (Session*)ptr;
                //重置用户超时计数
                client_reset_timer(fd);

                if(pre_event & EPOLLIN)
                {
                    if(read_msg(sn) >= 0)
                    {
                        handler(sn);
                    }
                    else
                    {
                        //错误处理
                    }


                }
                if(pre_event & EPOLLOUT)
                {
                    if(write_msg(sn) < 0)
                    {
                        
                        //错误处理
                    }
                }

                if(pre_event & (EPOLLHUP | EPOLLERR))
                {
                    //错误处理
                }


            }
        }
    }


    }




void on_login(Session* session, uid* user_id)
{
    auto weak = session->get_weak_ptr();
    // 捕获weak 获得 session的 消息槽    消息槽: 发送消息的权柄
    sendFn out = [weak](const packet& pkg)
    {
        if(auto s = weak.lock())
        {
            // 发送消息的逻辑 
            append_pkg(s.get(), pkg.type, pkg.body, pkg.len);
        }
    }
    // 将user_id 和 send_Fn 绑定
    send_slot_map.bind_send_fn(user_id, session->id, out);
    

}





int read_msg(Session* sn)
{
    //从内核缓冲区读取消息到用户态读缓冲区
    char* buf = sn-> read_buf + sn->read_pos;
    int left = MAX_BUF - sn->read_pos;

    while()
    {
        int n = recv(sn -> fd, buf, left, 0);
        if(n >0)
        {
            sn->read_pos += n;
            buf +=n;
            left-=n;
            if(left <= 0) break;
        }
        else if(n == 0)
        {
            return -1;  
        }
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return -1;
    }
    return 0;
    }
}




int write_msg(Session* sn)
{

    lock_guard<mutex> lk(sn->write_mtx);
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
    lock_guard<mutex> lk(sn->write_mtx);
    if(len + HEAD_LEN > MAX_BUF - sn->write_pos) {
        fprintf(stderr, "[append_pkg] BUFFER FULL! Dropping.\n");
        return;
    }
    if(len + HEAD_LEN > MAX_BUF - sn->write_pos)
    return ;
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
    return epoll_ctl(int epfd, EPOLL_CTL_MOD, fd, &ev)

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



//专门解析消息类型 执行相关操作
void type_handler(Session* sn, char type, char* body, int body_len)
{    
    if(sn->state == STATE_LOGIN)
    {
        if(type == 1)     //------------------------------用户上线通知
        {
            char user[USER_ID_LEN] = {0};
            strncpy(user, body, USER_ID_LEN - 1);
            user[USER_ID_LEN - 1] = '\0';
            sn->user_id = user;
            sn->state = STATE_NORMAL;
            {
                lock_guard<mutex> lk(online_table_mtx);
                online_table[user] = sn;
            }       
            printf("用户 %s 已上线\n", user);

            //注册消息槽
            on_login(sn, user);

            //发送历史消息
            Inbox_send(sn);
        }
    }else if (sn->state == STATE_NORMAL)//??????
    {  
        if(type == 3)         //-----------------------------     查询在线列表
        {
            char msg[MAX_BUF] = {0};
            lock_guard<mutex> lk(clients_mtx);
            if(online_table.empty())
            {
                strcpy(msg, "没有人在线喵~ 空悲切 ");
            }else
            {
                char tmp[512] = "在线用户有 :";
                int p = strlen(tmp);
                int remaining = sizeof(tmp) - p;
                for(const auto& it : online_table)
                {
                    int written = snprintf(tmp + p, remaining, " %s", it.first.c_str());
                    if(written <= 0 || written >= remaining)
                        break;
                    p += written;
                    remaining -= written;
                }
                strncpy(msg, tmp, sizeof(msg)-1);
            }
            append_pkg(sn, 3, msg, strlen(msg));
            
        }else if(type == 2) //-----------------------------  --- 用户发送消息 
        {
            char* c = body;
            while (*c != '?' && *c != '\0') 
            {
                    c++;
            }
            

            *c = '\0';
                c++;
            
            char msg_content[MAX_BUF] = {0};
            char target_user[USER_ID_LEN] = {0};
            strncpy(msg_content, c, sizeof(msg_content) - 1);
            msg_content[sizeof(msg_content) - 1] = '\0';
            strncpy(target_user, body, sizeof(target_user) - 1);
            target_user[sizeof(target_user) - 1] = '\0';

            printf("用户 %s 发送消息给 %s: %s\n", sn    ->user_id.c_str(), target_user, msg_content + 6);//神奇的操作

            Inbox_add(target_user, msg_content);
            notify_user(target_user);
        }else if (type == 4 )
            {
                printf("%s 申请进行游戏\n", sn->user_id.c_str());
                //加入在线玩家表
                game_manager.add_player_table(sn->user_id); 

            }else if (type == 5 )
            {
                printf("%s 选择出拳 %s\n", sn->user_id.c_str(), body);
                game_manager.handle_game_msg(sn, body, body_len);

            }

    }
}



void handler(Session* sn)
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
        type_handler(sn, type, body, body_len);

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


//释放客户端在线列表对象 关闭fd epoll_del
void close_client_session(int epfd, Session* sn)
{
    int fd = sn -> fd;
    epoll_del(epfd, fd);

    {
        lock_guard<std::mutex> lk(online_table_mtx);
        if(!sn->user_id.empty())
        {
            online_table.erase(sn->user_id);
            printf("用户%s 已下线\n", sn->user_id);
        }
    }
    // 将关闭fd写在这
    close(fd);
    delete(sn);
}


//处理客户端关闭时的资源释放
void free_resource(uid user_id, int epfd)
{
    //将此id 对应的所有对象全部释放 
    //注： id 具有唯一性

    //删除在线表内的用户
    close_client_session(epfd, user_id);

    //删除用户游戏实例
    game_manager.del_player_from_table(user_id);

    //清理对应SendFn
    account_table.delete_send_fn(user_id);
}


