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

        //发送游戏数据 
        Packet pkt;
        p1->out(pkt);
    }
    



}




void wait();



// (account_table_mtx 已随注册表内部化移除:注册表自带锁,见 server.h)


void GameManager::add_player_table(std::string user_id)
{
    std::shared_ptr<Player> p;
    // 先从注册表拷出该用户的发送权柄,复制进 Player —— 之后每次发消息不用再抢注册表的锁。
    // 注册表内部自带锁,调用方不要再额外加锁;曾经的外部锁 account_table_mtx 已移除。
    // TODO(R4 游戏模块):注意顺序 bug —— 此刻 p 还是空 shared_ptr,下面的 p->out 会
    // 解引用空指针;应把"取 fn"挪到 p 创建之后,重写 add_player_table 时一起修。
    if (auto opt_send = account_table.get_send_fn(user_id))
    {
        p->out = *opt_send;
    }
    else
    {
        // 注册表里查不到该用户(已下线/未登录):丢弃本次游戏申请
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
