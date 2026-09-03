#include "server.h"

#include <thread>

#define PORT 4000


//
int listen_fd = -1;




// 服务器开启入口  
int main()
{
    

int listen_fd = startup(&PORT);



/*所有线程启动*/
std::thread t1(connect_thread, listen_fd);
printf("Server running on port");
//连接线程




// 游戏线程
std:: thread t2(game_thread,);
printf("Game server has started");








}




