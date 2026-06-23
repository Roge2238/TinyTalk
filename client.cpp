#include <cstdio>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <thread>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>
#include <atomic>
using namespace std;

atomic<bool> g_running{true};

int recv_all(int sockfd, char* buf, int len)
{
    int received = 0;
    while (received < len) {
        int r = recv(sockfd, buf + received, len - received, 0);
        if (r <= 0) return -1;
        received += r;
    }
    return 0;
}

void recv_msg(int sockfd)
{
    char head[5];
    char buf[1025];
    while (g_running) {
        if (recv_all(sockfd, head, 5) < 0) break;
        int len = (head[1] << 24) | (head[2] << 16) | (head[3] << 8) | head[4];
        if (len <= 0 || len >= sizeof(buf)) continue;
        if (recv_all(sockfd, buf, len) < 0) break;
        buf[len] = '\0';
        printf("\n[收到消息] %s\n", buf);
        printf("输入消息: ");
        fflush(stdout);
    }
}

int send_pkg(int sockfd, char type, const char* str)
{
    int len = strlen(str);
    char head[5];
    head[0] = type;
    head[1] = (len >> 24) & 0xff;
    head[2] = (len >> 16) & 0xff;
    head[3] = (len >> 8) & 0xff;
    head[4] = len & 0xff;

    if (send(sockfd, head, 5, 0) < 0) return -1;
    if (send(sockfd, str, len, 0) < 0) return -1;
    return 0;
}

//查询在线用户列表
void check_list(char* id ,int sockfd)
{
 char buf[100];
 snprintf(buf, sizeof(buf), "%s查询在线用户列表", id);
 send_pkg(sockfd, 3, buf);
}


int main(int argc, char* argv[])
{
    int sockfd;
    struct sockaddr_in address;
    int result;
    char premsg[1024];
    char msg[1024];
    char id[10] = {0};
    char mainMsg[1130];
    char target_id[10];
    socklen_t len;

    // 从命令行参数获取用户ID，默认为"0001"
    if (argc > 1) {
        strncpy(id, argv[1], 9);
        id[9] = '\0';
    } else {
        strncpy(id, "0001", 9);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(4000);
    len = sizeof(address);
    result = connect(sockfd, (struct sockaddr*)&address, len);
    if (result == -1)
    {
        perror("连接失败");
        exit(1);
    }
    printf("已连接到服务器\n"); 

    thread p(recv_msg, sockfd);
    p.detach();

    // 发送用户ID（类型1）
    if (send_pkg(sockfd, 1, id) < 0) {
        printf("发送用户ID失败\n");
        close(sockfd);
        exit(1);
    }
    // Change: 删去上线信息  加入查询说明书功能
    printf("输入'o' 查看说明书\n");
    while (1) {
        printf("输入消息: ");
        fflush(stdout);
        fgets(premsg, sizeof(premsg), stdin);
        
        size_t len = strlen(premsg);
        if (len > 0 && premsg[len - 1] == '\n') {
            premsg[len - 1] = '\0';
        }

        // 检查退出命令
        if (strcmp(premsg, "quit") == 0 || strcmp(premsg, "exit") == 0)
        {
            printf("退出聊天\n");
            g_running = false;
            break;
        }

        if (strcmp(premsg, "o") == 0 )
        {
          printf("1)输入's' 查询在线列表\n2)输入'quit' or 'exit'退出\n3)输入 '@ + 用户id +消息' 发送消息 如'@0001 绷住'\n" );
          continue;
        } 
        if( strcmp(premsg, "s") == 0)
        {
         check_list(id, sockfd);
         continue;
        }

        if(premsg[0] != '@') 
        {
            printf("格式错误: 要以@开头喵~\n");
            continue;
        }
        
        char* space_pos = strchr(premsg, ' ');
        if(!space_pos)
        {
            printf("格式错误: ID后要加空格喵~\n");
            continue;
        }
        *space_pos = '\0';

        int id_len = space_pos - (premsg + 1);
        if (id_len <= 0 || id_len >= 9) {
            printf("格式错误: 目标ID长度无效喵~\n");
            continue;
        }

        
        strncpy(target_id, premsg + 1, sizeof(target_id) - 1);
        target_id[sizeof(target_id) - 1] = '\0';

        char* message_start = space_pos + 1;
        if (strlen(message_start) == 0) {
            printf("格式错误: 消息内容不能为空喵~\n");
            continue;
        }
        strncpy(msg, space_pos + 1, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';

        // 格式：目标ID?发送者ID: 消息内容
        snprintf(mainMsg, sizeof(mainMsg), "%s?%s: %s", target_id, id, msg);
        if (send_pkg(sockfd, 2, mainMsg) < 0) {
            printf("发送失败，连接可能已断开\n");
            break;
        }
    }

    close(sockfd);
    exit(0);
}
