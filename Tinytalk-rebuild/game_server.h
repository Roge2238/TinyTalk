// 目前只有一个游戏 先这么写

#include <queue>
#include <memory>
#include <mutex>


typedef struct Player
{
    std::string id;


};




class GameManager
{
    public:

    GameManager();
    ~GameManager();
    
    void match_player();
    

    private:
        // 匹配队列 从主线程来的都先进入这个
    std::queue<std:: shared_ptr<Player>> player_match_q;
    std::mutex  match_queue_mtx;







}







void game_thread();