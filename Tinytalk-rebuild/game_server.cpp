#include "game_server.h"

#include <chrono>

// ============================================================
// 游戏模块:当前是"能编译、不崩溃"的过渡状态。
// 匹配/对局的完整重写(匹配队列只存 uid、对局线程持 shared_ptr 保活、
// 玩家消息走 sendFn)见 DESIGN.md §9,放游戏模块轮次(R4)做;
// 现阶段 type4/5 尚未从网络层接线进来,对局逻辑不会被触发。
// ============================================================

// 全局游戏管理器(游戏线程与网络层下线回调都通过它)
GameManager game_manager;

//extern
extern std::atomic<bool> go_running;
extern Account_table account_table;

GameManager::GameManager() = default;
GameManager::~GameManager() = default;



//游戏线程入口 
void GameManager:: game_loop()
{
    match_player();
}




//这里有个问题很难绷 queue永远是后验逻辑 每次都是取队列元素后通过weak_ptr的lock 看是不是存活
//处理逻辑有点麻烦 先放这里不动 问ai看看有没有好的数据结构 
void GameManager:: match_player()
{
    while(go_running.load())
    {
        std::weak_ptr<Player> p1, p2;
        {
            std::unique_lock<std::mutex> lock(player_match_q_mtx);
            player_match_q_cv.wait(lock, [this] { return !player_match_q.empty(); });

            if (player_match_q.size() >= 2)
            {
                p1 = player_match_q.front();
                player_match_q.pop();
                p2 = player_match_q.front();
                player_match_q.pop();
            }
        }

        if (p1.expired() && p2.expired())
        {
            continue; // 两个玩家都已经掉线，继续等待
            // 对局线程持有 shared_ptr, 保证对局期间 Player 对象不会提前释放
            std::thread t(&GameManager::come_on_game, this, p1, p2);
            t.detach();
        }
    }
}



//加入游戏房间
// 对局线程。TODO(R4):目前不会被真正调到(type4/5 尚未接线);

void GameManager::come_on_game(std::weak_ptr<Player> p1, std::weak_ptr<Player> p2)
{
    auto s1 = p1.lock();
    auto s2 = p2.lock();
    if (!s1 || !s2)
        return;   // 有人已不在(掉线/被回收):对局直接结束

    while (go_running.load() && !s1->disconnected.load() && !s2->disconnected.load())
    {
        // TODO(R4):对局主循环 —— 等双方 updated、回合超时、胜负判定,
        // 结果用 Player.out(sendFn) 发送;对局期间由本线程持 shared_ptr 保活。
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    s1->in_game.store(false);
    s2->in_game.store(false);
}



void GameManager::add_player_table(std::string user_id)
{
    // 先确认用户在线且有发送权柄   不在线直接丢弃,不建对象不入队
    auto opt_send = account_table.get_send_fn(user_id);
    if (!opt_send)
        return;

    // 再拿/建 Player(表里有就复用,没有就 new)
    std::shared_ptr<Player> p;
    {
        std::lock_guard<std::mutex> lock(player_table_mtx);
        auto it = player_table.find(user_id);
        if (it != player_table.end())
            p = it->second;
        else
        {
            p = std::make_shared<Player>();
            p->id = user_id;
            player_table[user_id] = p;
        }
    }

    //  最后才把当前权柄拷进去
    p->out = *opt_send;

    


    /*加入匹配队列*/
    
    // 这个设计不知好不好 player_table里面存 Player的shared_ptr实例  
    // 匹配队列 player_match_q 里面只存 weak_ptr指针 
    // 设想到如果用户掉线 我们需要清理client 的Session 和 Player 所以将会进行 对PLayer实例的销毁 
    // 此时 Player_table找到 后删除销毁  避免对局玩家 和一个僵尸在匹配 用weak_ptr 观察对象存活状态  避免对局玩家和一个僵尸对局 再用disconnected检查一波   
    std::weak_ptr<Player> weak_p (p);

    {
        std::lock_guard<std::mutex> lock(player_match_q_mtx);
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




void GameManager:: del_player_from_table(std:: string user_id)
{

    {
        std::lock_guard<std::mutex> lock(player_table_mtx);
        auto it = player_table.find(user_id);
        if (it != player_table.end())
        {
            //erase 会调用shared_ptr的析构 完全没问题！！
            player_table.erase(it);
        }
    }

}



std::shared_ptr<Player> GameManager:: Get_player_from_table(std::string user_id)
{
    std::lock_guard<std::mutex> lock(player_table_mtx);
    auto it = player_table.find(user_id);
    if (it == player_table.end())
        return nullptr;
    return it->second;


}


void GameManager:: Update_player_GameData(char* data_buf)
{
    (void)data_buf;   // TODO(R4):游戏内数据(出拳等)的写入接口,随对局重写一起实现
}


//tearsession后回调 ： 设Player中的 disconnected状态为 0 
 void GameManager:: on_user_offline(const std::string& uid)
 {
    {
        std::lock_guard<std::mutex> lk(player_table_mtx);
        auto it = player_table.find(uid);
        if(it == player_table.end()) return;
        it -> second -> disconnected.store(true);
    }
 }

void game_thread()
{
    game_manager.game_loop();
}





void On_user_offline(const std::string& uid)
{
    game_manager.on_user_offline(uid);
}

