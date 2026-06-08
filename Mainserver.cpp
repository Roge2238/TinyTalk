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
using namespace std;

void error_die(const char* msg) {
    perror(msg);
    exit(1);
}

// 客户端连接信息
struct ClientInfo {
    int sockfd;
    string user_id;
    std::mutex send_mtx;  // 保护send操作
    std::atomic<bool> running{true};
};

// 全局资源
mutex mtx1;  // 保护Inbox
mutex clients_mtx;  // 保护OnlineClients
unordered_map<string, vector<string>> Inbox;
unordered_map<string, ClientInfo*> OnlineClients;

// 函数声明
void Inbox_add(const char*, const char*);
void Inbox_send(ClientInfo*);
int recv_all(int, char*, int);
int send_pkg(int, char, const char*, int);
int recv_user_id(int, char*, ClientInfo*);
void recv_msg_loop(int, ClientInfo*);
void recv_msg(int, ClientInfo*);
void task(int);
int startup(u_short*);
void handle_client_disconnect(ClientInfo*);
void notify_user(const string&);

// 发送数据包（带协议头部）
int send_pkg(int sockfd, char type, const char* str, int len) {
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

// 通知特定用户有新消息
void notify_user(const string& user_id) {
    ClientInfo* client = NULL;
   {
    lock_guard<mutex> lk(clients_mtx);
    auto it = OnlineClients.find(user_id);
    if (it != OnlineClients.end()) {
        client = it->second;
    }
   }
   if(client) Inbox_send(client);
}

// 处理客户端断开连接
void handle_client_disconnect(ClientInfo* client) {
    client->running = false;
    
    // 从在线列表中移除
    lock_guard<mutex> lk(clients_mtx);
    auto it = OnlineClients.find(client->user_id);
    if (it != OnlineClients.end() && it->second == client) {
        OnlineClients.erase(it);
        printf("用户 %s 已断开\n", client->user_id.c_str());
    }
    
    close(client->sockfd);
    delete client;
}

// 接收完整数据
int recv_all(int sockfd, char* buf, int len) {
    int received = 0;
    while (received < len) {
        int r = recv(sockfd, buf + received, len - received, 0);
        if (r <= 0) return -1;
        received += r;
    }
    return 0;
}

// 接收消息循环
void recv_msg_loop(int sockfd, ClientInfo* client) {
    char head[5];
    char buf[1024];
    char target[10];
    char msg[1024];

    while (client->running) {
        if (recv_all(sockfd, head, 5) < 0) break;
        
        int type = head[0];

        int len = (head[1] << 24) | (head[2] << 16) | (head[3] << 8) | head[4];
        if (len <= 0 || len >= sizeof(buf)) break;
        if (recv_all(sockfd, buf, len) < 0) break;
        
        buf[len] = '\0';
        if( type == 3) // 查询在线用户列表功能 ~
        {
            printf("%s\n", buf);
            lock_guard<mutex> lk(clients_mtx);
            if(OnlineClients.empty())
            {
                strncpy(msg, "没有人在线喵~ 空悲切 ", sizeof(msg) - 1);
                msg[sizeof(msg) - 1] = '\0';
            }else
            {
                char tmp[128] = "在线用户有 :";
                int pos = strlen(tmp);
                const int size = sizeof(tmp);
             for( const auto& it : OnlineClients )
             {
                const char* p = it.first.c_str();
                int ret = snprintf( tmp + pos, size, " %s", p);
                pos+= ret;
                if(pos > size) break;
             }
             strncpy(msg, tmp, sizeof(msg));
             msg[sizeof(msg) - 1] = '\0';
            }
            send_pkg(sockfd, 3, msg, strlen(msg));
            continue;
        }
        // 解析消息格式：目标ID?消息内容
        char* c = buf;
        while (*c != '?' && *c != '\0') {
            c++;
        }
        if (*c == '\0') continue;  // 格式错误
        
        *c = '\0';
        c++;
        strncpy(msg, c, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
        strncpy(target, buf, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
        
        printf("用户 %s 发送消息给 %s: %s\n", client->user_id.c_str(), target, msg + 5);
        
        // 存入目标用户收件箱
        Inbox_add(target, msg);
        
        // 立即通知目标用户（如果在线）
        notify_user(target);
    }
}

// 接收用户ID
int recv_user_id(int sockfd, char* user, ClientInfo* client) {
    char head[5];
    char buf[64];
    if (recv_all(sockfd, head, 5) < 0) return -1;
    if (head[0] != 1) return -1;
    int len = (head[1] << 24) | (head[2] << 16) | (head[3] << 8) | head[4];
    if (len <= 0 || len > 64) return -1;
    if (recv_all(sockfd, buf, len) < 0) return -1;

    buf[len] = '\0';
    strncpy(user, buf, 9);
    user[9] = '\0';
    
    // 注册到在线用户列表
    client->user_id = user;
    lock_guard<mutex> lk(clients_mtx);
    OnlineClients[user] = client;
    
    printf("用户 %s 已上线\n", user);
    return 0;
}

// 接收消息线程入口
void recv_msg(int sockfd, ClientInfo* client) {
    char user[10] = {0};
    if (recv_user_id(sockfd, user, client) == 0) {
        Inbox_send(client);
        recv_msg_loop(sockfd, client);
    }
    handle_client_disconnect(client);
}

// 添加消息到收件箱
void Inbox_add(const char* target, const char* msg) {
    lock_guard<mutex> lk(mtx1);
    Inbox[target].push_back(msg);
}

// 发送收件箱消息给客户端
void Inbox_send(ClientInfo* client) {
    if (!client->running) return;
    
    lock_guard<mutex> lk1(mtx1);
    lock_guard<mutex> lk2(client->send_mtx);
    
    auto it = Inbox.find(client->user_id);
    if (it == Inbox.end()) return;
    vector<string>& vec = it->second;
    if (vec.empty()) return;
    
    for (const string& s : vec) {
        if (send_pkg(client->sockfd, 3, s.c_str(), s.length()) < 0) {
            client->running = false;
            break;
        }
    }
    vec.clear();
}

// 处理单个客户端连接
void task(int sockfd) {
    ClientInfo* client = new ClientInfo();
    client->sockfd = sockfd;
    
    thread msg_thread(&recv_msg, sockfd, client);
    msg_thread.detach();
    
    // 主线程等待接收线程结束
    while (client->running) {
        this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// 服务器启动
int startup(u_short* port) {
    int httpd = socket(AF_INET, SOCK_STREAM, 0);
    if (httpd == -1) {
        error_die("socket");
    }
    
    struct sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int on = 1;
    if (setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        error_die("setsockopt failed");
    }
    
    if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0) {
        error_die("bind");
    }
    
    if (*port == 0) {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1) {
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }
    
    if (listen(httpd, 5) < 0) {
        error_die("listen");
    }
    
    return httpd;
}

int main() {
    int server_sock = -1;
    int client_sock = -1;
    u_short port = 4000;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    
    server_sock = startup(&port);
    printf("Server running on port %d\n", port);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_name, &client_name_len);
        if (client_sock == -1) {
            error_die("accept");
        }
        
        printf("新连接: %s\n", inet_ntoa(client_name.sin_addr));
        
        thread newthread(&task, client_sock);
        newthread.detach();
    }
    
    close(server_sock);
    return 0;
}
