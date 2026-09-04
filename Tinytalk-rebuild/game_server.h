// 目前只有一个游戏 先这么写

#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>

struct GameData
{
    int a;
}



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
    

    private:
        // 匹配队列 从主线程来的都先进入这个
    std::queue<std:: weak_ptr<Player>> player_match_q;
    std::mutex  match_queue_mtx;
    std::condition_variable player_match_q_cv;            // 队列有玩家时唤醒匹配线程
    std::unordered_map<std::string, std::shared_ptr<Player>> player_table;
    std::mutex player_table_mtx;





}







void game_thread();




void add_player_table(std::string user_id)
{
    std::shared_ptr<Player> p;
    {
        lock_gaurd<std::mutex>  lock(player_table_mtx);
        aut it = player_table.find(user_id);
        if(it! = player_table.end())
        {
            p = it->second;   // 复用已有玩家对象
        }else
        {
            p = std::make_shared<Player>();
            p->id = user_id;
            player_table[user_id] = p;
        }
    }

    std::weak_ptr<Player> weak_p (p);

    {
        lock_guard<std::mutex> lock(player_match_q_mtx);
        if(!p->queued.load() && !p->in_game.load())
        {
            p->queued.store(true);

            if(auto sp = weak_p.lock())
            {
                player_match_q.push(sp);
                player_match_q_cv.notify_one();
            }
        
            player_match_q.push(p);
            player_match_q_cv.notify_one();
        }

    }
}











