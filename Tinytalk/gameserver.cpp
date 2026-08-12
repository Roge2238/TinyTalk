#include "gameserver.h"
#include "server.h"

#define MAX_HOUSE_CNT 10

// 一回合等待双方出拳的超时时间(毫秒)
#define WAIT_CHOICE_TIMEOUT_MS 60000


GameManager game_manager;

extern std::atomic<bool> go_running;


void match_id_msg(Player* me, Player* other, const char* buf)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "%s %s", buf, other->id.c_str());
    append_pkg(me->ctx, 4, msg, strlen(msg) + 1);
}


// 掉线检查后发送消息, 避免访问已释放的 ClientCtx
void send_safe(const std::shared_ptr<Player>& p, char type, const char* msg)
{
    if (p->disconnected.load())
        return;
    append_pkg(p->ctx, type, msg, strlen(msg) + 1);
}



// 出拳约定: 1=石头 2=剪刀 3=布
// 返回: 0=平局, 1=p1 赢, 2=p2 赢
int caiquan_method(const char* p1_choice, const char* p2_choice)
{
    int c1 = atoi(p1_choice);
    int c2 = atoi(p2_choice);

    if (c1 == c2)   // 平局
        return 0;

    // 规则: 1石头赢2剪刀, 2剪刀赢3布, 3布赢1石头
    // 即 (出拳差 + 3) % 3 == 2 表示 p1 赢, == 1 表示 p2 赢
    // int diff = (p1_choice - p2_choice + 3) % 3;
    // if (diff == 2)
    // {
    //     *win = p1;
    //     *lose = p2;
    // }
    // else
    // {
    //     *win = p2;
    //     *lose = p1;
    int diff = (c1 - c2 + 3) % 3;
    return (diff == 2) ? 1 : 2;
}


// 等待双方出拳; 返回 0=就绪, 1=掉线, -1=超时
int wait_both_ready(const std::shared_ptr<Player>& p1, const std::shared_ptr<Player>& p2)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(WAIT_CHOICE_TIMEOUT_MS);
    while (go_running.load())
    {
        if (p1->disconnected.load() || p2->disconnected.load())
            return 1;
        if (p1->updated.load() && p2->updated.load())
            return 0;
        if (std::chrono::steady_clock::now() >= deadline)
            return -1;
        std::unique_lock<std::mutex> lk(p1->mtx);
        p1->cv.wait_for(lk, std::chrono::milliseconds(200));
    }
    return 1;
}


// 游戏房间(每个对局一个线程)
void come_on_game(std::shared_ptr<Player> p1, std::shared_ptr<Player> p2)
{
    char p1_choice[16] = {0};
    char p2_choice[16] = {0};

    while (go_running.load())
    {
        int wait_ret = wait_both_ready(p1, p2);
        if (wait_ret != 0)
        {
            if (wait_ret == -1)
            {
                send_safe(p1, 5, "超时, 本局结束");
                send_safe(p2, 5, "超时, 本局结束");
            }
            break;
        }

       
        {
            lock_guard<std::mutex> lk1(p1->mtx);
            strncpy(p1_choice, p1->msg_buf, sizeof(p1_choice) - 1);
            p1_choice[sizeof(p1_choice) - 1] = '\0';
            p1->updated.store(false);
        }
        {
            lock_guard<std::mutex> lk2(p2->mtx);
            strncpy(p2_choice, p2->msg_buf, sizeof(p2_choice) - 1);
            p2_choice[sizeof(p2_choice) - 1] = '\0';
            p2->updated.store(false);
        }

        int result = caiquan_method(p1_choice, p2_choice);
        if (result == 0)   // 平局, 重新出拳
        {
            send_safe(p1, 5, "平局");
            send_safe(p2, 5, "平局");
            continue;
        }

        send_safe(p1, 5, (result == 1) ? "你赢了 " : "你输了 ");
        send_safe(p2, 5, (result == 2) ? "你赢了 " : "你输了 ");
    }

    // 对局结束, 允许之后重新匹配
    p1->in_game.store(false);
    p2->in_game.store(false);
}


void GameManager::match_player_for_queue()
{
    while (go_running.load())
    {
        std::shared_ptr<Player> p1, p2;
        {
            std::unique_lock<std::mutex> lock(player_match_q_mtx);
            // 等待队列里至少有两名玩家; 顺带清理掉线玩家
            player_match_q_cv.wait(lock, [this]
            {
                if (!go_running.load())
                    return true;
                while (!player_match_q.empty() && player_match_q.front()->disconnected.load())
                    player_match_q.pop();
                return player_match_q.size() >= 2;
            });

            if (!go_running.load())
                break;
            if (player_match_q.size() < 2)   // 清理后不足两人, 继续等待
                continue;

            p1 = player_match_q.front();
            player_match_q.pop();
            p2 = player_match_q.front();
            player_match_q.pop();
        }

        p1->queued.store(false);
        p2->queued.store(false);
        p1->in_game.store(true);
        p2->in_game.store(true);

        const char* match_success_msg = "匹配成功 你的对手是 --";
        match_id_msg(p1.get(), p2.get(), match_success_msg);
        match_id_msg(p2.get(), p1.get(), match_success_msg);

        // 对局线程持有 shared_ptr, 保证对局期间 Player 对象不会提前释放
        std::thread t(come_on_game, p1, p2);
        t.detach();
    }
}


void GameManager::add_player_list(ClientCtx* ctx)
{
    std::shared_ptr<Player> p;
    {
        lock_guard<std::mutex> lock(player_map_mtx);
        auto it = player_map.find(ctx);
        if (it != player_map.end())
        {
            p = it->second;   // 复用已有玩家对象
        }
        else
        {
            p = std::make_shared<Player>();
            p->ctx = ctx;
            p->id = ctx->user_id;
            player_map[ctx] = p;
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


void GameManager::remove_player_list(ClientCtx* ctx)
{
    std::shared_ptr<Player> p;
    {
        lock_guard<std::mutex> lock(player_map_mtx);
        auto it = player_map.find(ctx);
        if (it != player_map.end())
        {
            p = it->second;
            player_map.erase(it);
        }
    }

    if (p)
    {
        p->disconnected.store(true);
        p->cv.notify_all();               // 唤醒对局线程结束本轮
        player_match_q_cv.notify_one();   // 唤醒匹配线程清理队列
    }
}


void GameManager::handle_game_msg(ClientCtx* ctx, const char* body, int len)
{
    std::shared_ptr<Player> p;
    {
        lock_guard<std::mutex> lock(player_map_mtx);
        auto it = player_map.find(ctx);
        if (it == player_map.end())
            return;   // 未申请过游戏, 忽略
        p = it->second;
    }

    if (!p->in_game.load())
    {
        // 不在对局中  视为加入匹配
        lock_guard<std::mutex> lock(player_match_q_mtx);
        if (!p->queued.load())
        {
            p->queued.store(true);
            player_match_q.push(p);
            player_match_q_cv.notify_one();
        }
        return;
    }

    // 对局中  保存出拳并通知对局线程
    {
        lock_guard<std::mutex> lock(p->mtx);
        size_t n = (size_t)len;
        if (n > sizeof(p->msg_buf) - 1)
            n = sizeof(p->msg_buf) - 1;
        memcpy(p->msg_buf, body, n);
        p->msg_buf[n] = '\0';
        p->updated.store(true);
    }
    p->cv.notify_all();
}


int GameManager::get_player_cnt()
{
    lock_guard<std::mutex> lock(player_map_mtx);
    return (int)player_map.size();
}


std::shared_ptr<Player> GameManager::find_player(ClientCtx* ctx)
{
    lock_guard<std::mutex> lock(player_map_mtx);
    auto it = player_map.find(ctx);
    if (it == player_map.end())
        return nullptr;
    return it->second;
}


void GameService() // 游戏服务线程
{
    game_manager.match_player_for_queue();
}
