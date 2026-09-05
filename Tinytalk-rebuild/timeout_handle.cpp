#include "timeout_handle.h"
#include "server.h"


extern Account_table account_table;


void handle_timeout()
{
    uint64_t now = get_now_ms();

    for(auto &pair : account_table)
    {
        Session* tmp = pair.second;
        if(tmp->fd == -1) continue;

        if(tmp->deadline_ms <= now)
        {
            printf(" %s 超时断开\n", tmp->user_id);

            free_resource(tmp->user_id, epfd);
            
        }
    }

    //重置闹钟
    refresh_alarm();


}



void refresh_alarm()
{
    uint64_t now = get_now_ms();
    uint64_t nearest = UINT64_MAX;

    for(auto &pair : account_table)
    {
        Session* tmp = pair.second;
        if(tmp->deadline_ms < nearest)
        {
            nearest = tmp->deadline_ms;
        }
    }

    if(nearest == UINT64_MAX)
    {
        //关闭闹钟 
    }



    set_alarm(nearest);
}




void set_alarm(uint64_t nearest)
{



}






void client_reset_timer()
{




}

