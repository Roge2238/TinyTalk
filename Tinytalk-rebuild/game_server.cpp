#include "game_server.h"



void GameManager::add_player_list(Session* sn)
{
    std::shared_ptr<Player> p;
    {
        lock_guard<std::mutex> lock(player_table_mtx);
        auto it = player_table.find(sn->user_id);
        if (it != player_table.end())
        {
            p = it->second;   // 复用已有玩家对象
        }
        else
        {
            p = std::make_shared<Player>();
            p->ctx = sn;
            p->id = sn->user_id;
            player_table[sn->user_id] = p;
        }
    }

    {
        lock_guard<std::mutex> lock(player_match_q_mtx);
        if (!p->queued.load() && !p->in_game.load())
        {
            p->queued.store(true);
            player_match_q.push(p);
            player_match_q_cv.notify_one();
        }
    }
}