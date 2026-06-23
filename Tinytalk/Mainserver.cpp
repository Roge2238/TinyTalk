#include "server.h"
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
        
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
         if (nfds < 0) error_die("epoll_wait");

        for(int i = 0; i < nfds; i++)
        {
            bool need_close = false;
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
//2026 6.6 来来来 直接一个recv 搞个微信出来试试喵 

//2026 6.9 学了epoll 准备改造 


//2026 6.13 简单用epoll重写了一下 感觉有点bug  问了下ai 解决不了 感觉架构有大问题 过几天再写写看 期末周了喵 谁能帮帮我 喵

//2026 6.23 用cmake重新构建了一下 便于维护