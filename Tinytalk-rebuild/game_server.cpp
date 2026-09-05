#include "game_server.h"



void GameMananger::add_player_table(std::string user_id)
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


    /*加入匹配队列*/
    
    // 这个设计不知好不好 player_table里面存 Player的shared_ptr实例  
    // 匹配队列 player_match_q 里面只存 weak_ptr指针 
    // 设想到如果用户掉线 我们需要清理client 的Session 和 Player 所以将会进行 对PLayer实例的销毁 
    // 此时 Player_table找到 后删除销毁  避免对局玩家 和一个僵尸在匹配 用weak_ptr 观察对象存活状态  避免对局玩家和一个僵尸对局 再用disconnected检查一波   
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




void GameManager:: del_player_from_table(std:: string user_id)
{

    {
        lock_guard<std::mutex> lock(player_table_mtx);
        auto it = player_table.find(user_id);
        if (it != player_table.end())
        {
            //erase 会调用shared_ptr的析构 完全没问题！！
            player_table.erase(it);
        }
    }

}