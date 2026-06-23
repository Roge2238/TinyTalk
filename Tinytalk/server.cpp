#include "server.h"
#include "inbox.h"

mutex clients_mtx;
unordered_map<string, ClientCtx*> OnlineClients;
int epfd = -1;

void error_die(const char* msg)
{
    perror(msg);
    exit(1);
}





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
    if (total == 0) {
        // 确保不会不必要地监听EPOLLOUT
        epoll_mod(epfd, fd, EPOLLIN | EPOLLET, ct);
        return 0;
    }

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
         memset(ct->write_buf + ct->write_pos, 0, MAX_BUF - ct->write_pos);

    }
    return 0;
}


//包装协议 放入写缓冲区
void append_pkg(ClientCtx* ct, char type, const char* msg, int len)
{
   
    if(len + HEAD_LEN > MAX_BUF - ct->write_pos) {
        fprintf(stderr, "[append_pkg] BUFFER FULL! Dropping.\n");
        return;
    }
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
    if(epoll_mod(epfd, ct->fd, EPOLLIN | EPOLLOUT | EPOLLET, ct) < 0)
        perror("[append_pkg] epoll_mod failed");
}


//处理
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
         if(body_len < 0 || body_len > MAX_BUF)
        {
            fprintf(stderr, "[handler] Invalid body_len: %d\n", body_len);
            ct->read_pos = 0;
            memset(ct->read_buf, 0, MAX_BUF);
            return;
        }

         if( pos + body_len > len)
        {
            pos -= HEAD_LEN;
            break;
        }
       
            
        char* body = buf + pos;
        
        if(ct->state == STATE_LOGIN)
        {
            if(type == 1)
            {
                char user[USER_ID_LEN] = {0};
                strncpy(user, body, USER_ID_LEN - 1);
                user[USER_ID_LEN - 1] = '\0';
                ct->user_id = user;
                ct->state = STATE_NORMAL;
                {
                    lock_guard<mutex> lk(clients_mtx);
                    OnlineClients[user] = ct;
                }
                printf("用户 %s 已上线\n", user);
                Inbox_send(ct);
            }
        }else if (ct->state == STATE_NORMAL)//??????
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
                    char tmp[512] = "在线用户有 :";
                    int p = strlen(tmp);
                    int remaining = sizeof(tmp) - p;
                    for(const auto& it : OnlineClients)
                    {
                        int written = snprintf(tmp + p, remaining, " %s", it.first.c_str());
                        if(written <= 0 || written >= remaining)
                            break;
                        p += written;
                        remaining -= written;
                    }
                    strncpy(msg, tmp, sizeof(msg)-1);
                }
                append_pkg(ct, 3, msg, strlen(msg));
                
            }else if(type == 2)
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
        
                printf("用户 %s 发送消息给 %s: %s\n", ct->user_id.c_str(), target_user, msg_content + 6);//神奇的操作
        
                Inbox_add(target_user, msg_content);
                notify_user(target_user);
            }
        }
        pos += body_len;
    }
    
    if(pos > 0 && pos < len)
    {
        memmove(ct->read_buf, ct->read_buf + pos, len - pos);
        ct->read_pos = len - pos;
        memset(ct->read_buf + ct->read_pos, 0, MAX_BUF - ct->read_pos);
    }
    else if(pos >= len)
    {
        ct->read_pos = 0;
        memset(ct->read_buf, 0, MAX_BUF);
    }
}



//读取到读缓冲区
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