#include "server.h"
#include "game_server.h"

#define MAX_EPOLL_EVENT 10 // 待改




void connect_thread(int listen_fd)
{

    int epfd = epoll_create1(0);
    if(epfd < 0) error_doe("epoll_create");
    
    //开始只有listen_fd 原始监听

    epoll_add(epfd, listen_fd, EPOLLIN | EPOLLET, nullptr);

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



int read_msg(Session* sn)
{
    //从内核缓冲区读取消息到用户态读缓冲区
    char* buf = sn-> read_buf + sn->read_pos;
    int left = MAX_BUF - sn->read_pos;

    while()
    {
        int n = recv(sn -> sn, buf, left, 0);
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
    //向内核缓冲区写入消息
    return 0;
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




void handler(Session* sn)
{
    // 处理client的消息

    //从sn 的用户态缓冲区读取 处理 
    






}