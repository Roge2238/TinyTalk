#include "common.h"


enum GameIndex
{
    caiquan = 1
};








struct Player
{
    ClientCtx* ctx;
    string id;
    int fd;
    GameIndex gameindex;
    char* write_buf;
    char* read[10];
    bool updated = 0;
};



