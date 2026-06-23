#ifndef SERVER_H
#define SERVER_H

#include "common.h"

extern mutex clients_mtx;
extern unordered_map<string, ClientCtx*> OnlineClients;
extern int epfd;

// 函数声明
void error_die(const char* msg);
int startup(u_short* port);
void notify_user(const string& user_id);
int epoll_add(int epfd, int fd, uint32_t events, ClientCtx* ct);
int epoll_mod(int epfd, int fd, uint32_t events, ClientCtx* ct);
int epoll_del(int epfd, int fd);
void close_client(int epfd, ClientCtx* ct);
int read_msg(ClientCtx* ct);
int write_msg(ClientCtx* ct);
void append_pkg(ClientCtx* ct, char type, const char* msg, int len);
void handler(ClientCtx* ct);


#endif






