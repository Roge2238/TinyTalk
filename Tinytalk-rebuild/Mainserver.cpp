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
    // 日志即时可见:stdout 默认全缓冲,重定向到文件时被 kill/崩溃会丢日志
    setvbuf(stdout, nullptr, _IONBF, 0);

    u_short port = PORT;
    int listen_fd = startup(&port);
    printf("Server running on port %d\n", (int)port);

    /*所有线程启动*/

    // io 线程:accept / 收发 / 分发 / 超时扫描。
    // join 住 —— 进程生命周期跟随它;将来优雅退出时置 go_running=false 即可放行。
    std::thread t1(connect_thread, listen_fd);

    // 游戏线程:匹配 + 对局。现阶段 type4/5 还没从网络层接线,线程空转等待队列。
    std::thread t2(game_thread);
    t2.detach();

    // TODO(视频):std::thread t3(video_thread, listen_fd);

    t1.join();
    return 0;
}
