#ifndef GAMESERVER_H
#define GAMESERVER_H

#include "common.h"
#include <memory>
#include <queue>

struct Player;

enum GameIndex
{
    caiquan = 1
};


class GameManager
{
public:
    int game_thread_cnt = 0;

    GameManager();
    ~GameManager();
    void add_player_list(ClientCtx* ctx);                              
    void remove_player_list(ClientCtx* ctx);                           // 掉线清理
    void handle_game_msg(ClientCtx* ctx, const char* body, int len);   // 对局中接收出拳
    void match_player_for_queue();                                     // 匹配主循环
    std::shared_ptr<Player> find_player(ClientCtx* ctx);
    int get_player_cnt();

private:
    std::queue<std::shared_ptr<Player>> player_match_q;   // 匹配队列
    std::mutex player_match_q_mtx;
    std::condition_variable player_match_q_cv;            // 队列有玩家时唤醒匹配线程
    std::unordered_map<ClientCtx*, std::shared_ptr<Player>> player_map;
    std::mutex player_map_mtx;
};

inline GameManager::GameManager() = default;

inline GameManager::~GameManager() = default;




struct Player
{
    ClientCtx* ctx = nullptr;
    string id;
    GameIndex gameindex = caiquan;
    char msg_buf[32] = {0};                  // 出拳内容, 由 handle_game_msg 写入

    std::atomic<bool> updated{false};       // 本回合是否已出拳
    std::atomic<bool> queued{false};        // 是否正在匹配队列中
    std::atomic<bool> in_game{false};       // 是否已进入对局
    std::atomic<bool> disconnected{false};  // 是否已掉线

    std::mutex mtx;                         //  msg_buf 的锁
    std::condition_variable cv;             // 唤醒等待出拳的对局线程
};

// 游戏服务线程入口
void GameService();

#endif
