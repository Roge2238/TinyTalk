#include "server.h"


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
        }


    }





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
