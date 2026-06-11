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
using namespace std;

const int MAX_BUF = 1024;
const int HEAD_LEN = 5;
const int USER_ID_LEN = 10;
const int MAX_EVENTS = 1024;

enum ClientState
{
    STATE_LOGIN = 0,  
    STATE_NORMAL
};

struct ClientCtx
{
  int fd;
  string user_id;
  ClientState state;
  
  char read_buf[MAX_BUF];
  int read_pos;

  char write_buf[MAX_BUF];
  int write_pos;

  ClientCtx()
    :fd(-1), state(STATE_LOGIN), read_pos(0), write_pos(0)
    {
        memset(read_buf, 0, MAX_BUF);
        memset(write_buf, 0, MAX_BUF);
    }

};

void error_die(const char* msg)
{
    perror(msg);
    exit(1);
}


// 全局资源
mutex mtx_box;  // Inbox锁
mutex clients_mtx;  // 保护OnlineClients
unordered_map<string, vector<string>> Inbox;
unordered_map<string, ClientCtx*> OnlineClients;

int epfd = -1;

// 函数声明
void Inbox_add(const char*, const char*);
void Inbox_send(ClientCtx*);
int startup(u_short*);
void notify_user(const string&);
int  epoll_add(int , int , uint32_t , ClientCtx* );
void handler(ClientCtx* );
int read_msg(ClientCtx* );
int write_msg(ClientCtx *);
void append_pkg(ClientCtx*, char, const char*, int );
int  epoll_mod(int , int , uint32_t , ClientCtx* );
void close_client(int , ClientCtx*); 
int epoll_del(int , int );  




int epoll_del(int epfd, int fd )
{
    return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr); 
}



void close_client(int epfd, ClientCtx* ct) //关闭
{
    int fd = ct->fd;
    epoll_del(epfd, fd);

    {
        lock_guard<mutex> lk(clients_mtx);
        if(!ct->user_id.empty())
        {
            OnlineClients.erase(ct->user_id);
            printf("用户 %s 已下线\n", ct->user_id.c_str());
        }
    }

    close(fd);
    delete(ct);
}



int write_msg(ClientCtx* ct)
{

    int sent = 0;
    int total = ct->write_pos; 
    int fd = ct->fd;
    while(sent < total)
    {
        int n = send(fd, ct->write_buf + sent, total - sent,  MSG_NOSIGNAL);
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
        memset(ct->write_buf, 0, MAX_BUF);
        ct->write_pos = 0;
        epoll_mod(epfd, fd, EPOLLIN | EPOLLET, ct);
    }
    else
    {
        int remain = total - sent;
        memmove(ct->write_buf, ct->write_buf + sent, remain);
        ct->write_pos = remain;
    }
    return 0;
}



void append_pkg(ClientCtx* ct, char type, const char* msg, int len)
{
    if(len + HEAD_LEN > MAX_BUF - ct->write_pos)
    return ;
    char head[HEAD_LEN];
    head[0] = type;
    head[1] = (len >> 24) & 0xFF;
    head[2] = (len >> 16) & 0xFF;
    head[3] = (len >> 8) & 0xFF;
    head[4] = (len)     & 0xFF;
    memcpy(ct -> write_buf + ct->write_pos, head, HEAD_LEN);
    ct->write_pos += HEAD_LEN; 
    memcpy(ct -> write_buf + ct->write_pos, msg, len);
    ct->write_pos += len;
    epoll_mod(epfd, ct->fd, EPOLLIN | EPOLLOUT | EPOLLET, ct);
}



void handler(ClientCtx* ct)
{
    char* buf = ct->read_buf;
    int len = ct->read_pos;
    int pos = 0;
    while( pos + HEAD_LEN <= len)
    {
        int type = buf[pos];
        int body_len = (buf[pos + 1] << 24) | (buf[pos + 2] << 16) | (buf[pos + 3] << 8) | buf[pos + 4];
        pos += HEAD_LEN;
        
        if( pos + body_len > len)
            break;
            
        char* body = buf + pos;
        
        if(ct->state == STATE_LOGIN)
        {
            if(type == 1)
            {
                char user[USER_ID_LEN] = {0};
                strncpy(user, body, USER_ID_LEN - 1);
                ct->user_id = user;
                ct->state = STATE_NORMAL;
                {
                    lock_guard<mutex> lk(clients_mtx);
                    OnlineClients[user] = ct;
                }
                printf("用户 %s 已上线\n", user);
                Inbox_send(ct);
            }
            pos += body_len;
        }else if  (ct->state == STATE_NORMAL)
        {
            if(type == 3)
            {
                char msg[MAX_BUF] = {0};
                lock_guard<mutex> lk(clients_mtx);
                if(OnlineClients.empty())
                {
                    strcpy(msg, "没有人在线喵~ 空悲切 ");
                }else
                {
                    char tmp[128] = "在线用户有 :";
                    int p = strlen(tmp);
                    for(const auto& it : OnlineClients)
                    {
                        p += snprintf(tmp + p, sizeof(tmp) - p," %s", it.first.c_str());
                    }
                    strncpy(msg, tmp, sizeof(msg)-1);
                }
                append_pkg(ct, 3, msg, strlen(msg));
                pos += body_len;
                continue;
            }else
            {
                char* c = body;
                while (*c != '?' && *c != '\0') 
                {
                     c++;
                }
                if (*c == '\0') 
                {
                    pos += body_len;
                    continue;
                }
        
                *c = '\0';
                 c++;
                
                char msg_content[MAX_BUF] = {0};
                char target_user[USER_ID_LEN] = {0};
                strncpy(msg_content, c, sizeof(msg_content) - 1);
                msg_content[sizeof(msg_content) - 1] = '\0';
                strncpy(target_user, body, sizeof(target_user) - 1);
                target_user[sizeof(target_user) - 1] = '\0';
        
                printf("用户 %s 发送消息给 %s: %s\n", ct->user_id.c_str(), target_user, msg_content);
        
                Inbox_add(target_user, msg_content);
                notify_user(target_user);
            }
        }
        pos += body_len;
    }
    
    if(pos > 0 && pos < len)
    {
        memmove(ct->read_buf, ct->read_buf + pos, len - pos);
    }
    ct->read_pos = len - pos;
}



//读取到缓冲区
int read_msg(ClientCtx* ct)
{
    char* buf = ct->read_buf + ct->read_pos;
    int left = MAX_BUF - ct->read_pos;

    while(1)
    {
        int n = recv(ct->fd, buf, left, 0);
        if( n > 0)
        {
            ct->read_pos += n;
            buf += n;
            left -=n;
            if(left <= 0) break;
        }
        else if(n == 0)
        {
            return -1;  
        }
        else{
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            break;
            return -1;
        }
    }
    return 0;

}

int  epoll_add(int epfd, int fd, uint32_t events, ClientCtx* ct)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = ct;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}


//修改监听事件 -> 触发写 开始发送信息
int  epoll_mod(int epfd, int fd, uint32_t events, ClientCtx* ct)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = ct;
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}



// 通知特定用户有新消息
void notify_user(const string& user_id) {
    ClientCtx* client = NULL;
   {
    lock_guard<mutex> lk(clients_mtx);
    auto it = OnlineClients.find(user_id);
    if (it != OnlineClients.end()) {
        client = it->second;
    }
   }
   if(client) Inbox_send(client);
}



// 添加消息到收件箱
void Inbox_add(const char* target, const char* msg) {
    lock_guard<mutex> lk(mtx_box);
    Inbox[target].push_back(msg);
}

// 发送收件箱消息给客户端
void Inbox_send(ClientCtx* client) {
     
    lock_guard<mutex> lk1(mtx_box);
    
    auto it = Inbox.find(client->user_id);
    if (it == Inbox.end() || it->second.empty()) return;
    vector<string>& vec = it->second;
    
    for (const string& s : vec) {
        append_pkg(client, 3, s.c_str(), s.size());
    }
    vec.clear();
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

int main() {

     u_short port = 4000;

     int listen_fd = startup(&port);
     printf("Server running on port %d\n", port);


     epfd = epoll_create1(0);
     if(epfd < 0) error_die("epoll_create");

     epoll_add(epfd, listen_fd, EPOLLIN | EPOLLET, nullptr);
    
     struct epoll_event events[MAX_EVENTS];

    while(1)
    {
        bool need_close = false;
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
         if (nfds < 0) error_die("epoll_wait");

        for(int i = 0; i < nfds; i++)
        {
            void* ptr = events[i].data.ptr;
            uint32_t pre_event = events[i].events;

            if(ptr == nullptr)
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
                        perror("accept failed");
            
                    }
                    printf("新连接: %s\n", inet_ntoa(client_addr.sin_addr));
                    
                    ClientCtx* ct = new ClientCtx();
                    ct->fd = client_fd;
                    epoll_add(epfd, client_fd, EPOLLIN | EPOLLET, ct);
                }
            }
            else 
            {
                ClientCtx* ct = (ClientCtx*)ptr;

                if(pre_event & EPOLLIN)
                {
                    if(read_msg(ct) < 0)
                    {
                        need_close = true;
                    }
                    else
                    {
                        handler(ct);
                    }
                }
                if(!need_close && (pre_event & EPOLLOUT))
                {
                    if(write_msg(ct) < 0)
                        need_close = true;
                }
                if ((pre_event & (EPOLLHUP | EPOLLERR)) || need_close)
                {
                    close_client(epfd, ct);
                }
            }
        }
    }
    
    close(listen_fd);
    close(epfd);
    return 0;
}