// 目前只有一个游戏 先这么写


#include "server.h"

#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>


typedef struct GameFrame
{

};



// 思考良久 接收用户游戏数据信息采用异步  type_handler里面对应 消息类型直接拷贝进GameData 
// 之所以不用类似sendFn的 readFn 因为 会存在与handler函数的竞争 干涉其他类型消息的处理 且如何分离游戏类型消息很复杂

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
    std::shared_ptr<Player> Get_player_from_table(std::string user_id);
    void game_thread();
    void come_on_game(std::weak_ptr<Player> p1, std::weak_ptr<Player> p2);
    void Update_player_GameData(char* data);

    private:
    
        // 匹配队列 从主线程来的都先进入这个
    std::queue<std:: weak_ptr<Player>> player_match_q;
              // 队列有玩家时唤醒匹配线程
    std::unordered_map<std::string, std::shared_ptr<Player>> player_table;
    
    std::mutex player_table_mtx;
    std::mutex  player_match_q_mtx;
    std::condition_variable player_match_q_cv;  




};






















