#include "timeout_handle.h"

#define MAX_CLIENTS 64


void handle_timeout()
{
    uint64_t now = get_now_ms();

    for(int i = o; i< MAX_CLIENTS; i++)
    {
        Session* tmp = &client_sessions[i];
        if(tmp ->fd == -1) continue;


        if(tmp-> deadline_ms <= now)
        {
            printf(" %s 超时断开\n", tmp=>user_id);
            //epoll_DEL

            close(tmp->fd);
        }
    }

    //重置闹钟
    refresh_alarm();


}



void refresh_alarm()
{
    uint64_t now = get_now_ms();
    uint64_t nearest = UINT64_MAX;

    for(int i = 0; i < MAX_CLIENT; i++ )
    {
        Session* tmp = client_sessions[i];
        if(tmp -> deadline_ms < nearest)
        {
            nearest = tmp -> deadline_ms;
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

