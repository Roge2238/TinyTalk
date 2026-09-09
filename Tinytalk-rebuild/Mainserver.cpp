#include "server.h"
#include "game_server.h"

#include <atomic>
#include <cstdio>
#include <thread>

#define PORT 4000


std::atomic<bool> go_running{true};

// 服务器开启入口
int main()
{
    
    setvbuf(stdout, nullptr, _IONBF, 0);

    u_short port = PORT;
    int listen_fd = startup(&port);
    printf("Server running on port %d\n", (int)port);

    /*所有线程启动*/

    
    std::thread t1(connect_thread, listen_fd);

    // 游戏线程:匹配 + 对局
    std::thread t2(game_thread);
    t2.detach();

    // TODO(视频):std::thread t3(video_thread, listen_fd);

    t1.join();
    return 0;
}
