#ifndef COMMON_H
#define COMMON_H
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
#include <sys/epoll.h>
using namespace std;

const int MAX_BUF = 1024;
const int HEAD_LEN = 5;
const int USER_ID_LEN = 10;
const int MAX_EVENTS = 1024;

enum ClientState
{
    STATE_LOGIN = 0,  
    STATE_NORMAL
};

struct ClientCtx
{
  int fd;
  string user_id;
  ClientState state;
  
  char read_buf[MAX_BUF];
  int read_pos;

  char write_buf[MAX_BUF];
  int write_pos;

  ClientCtx()
    :fd(-1), state(STATE_LOGIN), read_pos(0), write_pos(0)
    {
        memset(read_buf, 0, MAX_BUF);
        memset(write_buf, 0, MAX_BUF);
    }

};


#endif