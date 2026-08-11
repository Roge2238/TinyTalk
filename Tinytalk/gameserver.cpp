#include "gameserver.h"
#include "server.h"
#include <queue>
#include <mutex>
#include <thread>

#define MAX_HOUSE_CNT 10



std:: queue<Player> player_q;
std:: mutex player_q_mtx;


void match_id_msg(Player* me, Player* other, char* buf)
{
    char msg[32];
    string other_id = other->id;
    snprintf(msg, sizeof(msg), "%s %s",buf, other_id );
    append_pkg(me->ctx, 4, msg, sizeof(msg));
}


void caiquan_method(Player* win, Player* lose, Player* p1, Player* p2)  // ------猜拳 判断逻辑
{



}



void come_on_game(Player* p1 , Player* p2)
{
    char* p1_buf = p1->write_buf;
    char* p2_buf = p2->write_buf;

    char p1_ans[10];
    char p2_ans[10];//玩家输入的操作  游戏选择数据
    while (1)  // ----先只弄猜拳
    {
        Player* win, *lose;
        if(!p1-> updated || !p2-> updated)
        return;
        p1 -> updated = false;
        p2 -> updated = false;
        strncpy();

        caiquan_method(win, lose, p1, p2);// 执行输赢判断 

        append_pkg(win->ctx, 5, "你赢了 ", sizeof("你赢了 "));
        append_pkg(lose->ctx, 5, "你输了 ", sizeof("你输了 "));
    }



}




extern std::atomic<bool> go_running;

void match_player_for_queue()
{

    while(go_running)
    {
        Player p1, p2;
        {
            lock_guard<mutex> lock(player_q_mtx);
        
            // 队列玩家不足两人，直接返回
            if (player_q.size() < 2)
                return;


            p1 = player_q.front();
            player_q.pop();
            
            
            p2 = player_q.front();
            player_q.pop();
        }
            

        char* match_success_msg = "匹配成功 你的对手是 --";
        
        match_id_msg(&p1, &p2, match_success_msg);
        match_id_msg(&p2, &p1, match_success_msg);

        thread t (come_on_game, &p1, &p2);
            t.detach();
      }

}












