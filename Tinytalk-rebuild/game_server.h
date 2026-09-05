// 目前只有一个游戏 先这么写


#include "server.h"

#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>

struct GameData
{
    int a;
};



typedef struct Player
{
    std::string id;
    GameData data;
    std::atomic<bool> updated{false};       // 本回合是否已出拳
    std::atomic<bool> queued{false};        // 是否正在匹配队列中
    std::atomic<bool> in_game{false};       // 是否已进入对局
    std::atomic<bool> disconnected{false};  // 是否已掉线
};




class GameManager
{
    public:

    GameManager();
    ~GameManager();
    
    void match_player();
    void add_player_table(std::string user_id);
    void del_player_from_table(std::string user_id);

   

    private:
    
        // 匹配队列 从主线程来的都先进入这个
    std::queue<std:: weak_ptr<Player>> player_match_q;
              // 队列有玩家时唤醒匹配线程
    std::unordered_map<std::string, std::shared_ptr<Player>> player_table;
    
    std::mutex player_table_mtx;
    std::mutex  player_match_q_mtx;
    std::condition_variable player_match_q_cv;  




};






















