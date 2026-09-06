#include "game_server.h"



extern std::atomic<bool> go_running;

extern Account_table account_table;


//游戏线程入口 
void GameManager:: game_thread()
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
void GameManager::come_on_game(std::weak_ptr<Player> p1, std::weak_ptr<Player> p2)
{
    while (go_running.load())
    {
        //等待玩家的操作数据
        //以Player的update 为信号

        //if(p1.update() && p2 .update()) 

        game_method(); // 游戏逻辑判断


    }
    



}




void wait();






void GameManager::add_player_table(std::string user_id)
{
    std::shared_ptr<Player> p;
    //先从send_slot_map里面找到注册的对应的sendfn 放入player结构体 
    //这是我自认为很细节的一个点 复制进player后 避免对象频繁访问send_slot_map拿锁找fn  特别是很多对象有通信需求的情况下
    auto opt_send = account_table.get_send_fn(user_id);
    if(opt_send.has_value())
    {
        //复制进Player实例
        sendFn tmp = opt_send.value();
        p->out = tmp;
    }
    else
    {
        // 没有这个玩家，已经掉线，直接丢弃数据包
    }
    {
        std::lock_guard<std::mutex>  lock(player_table_mtx);
        auto it = player_table.find(user_id);
        if(it != player_table.end())
        {
            p = it->second;   // 复用已有玩家对象
        }else
        {
            p = std::make_shared<Player>();
            p->id = user_id;
            player_table[user_id] = p;
        }
    }


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



}