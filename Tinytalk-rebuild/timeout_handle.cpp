#include "timeout_handle.h"

#include <cstdio>
#include <chrono>

// ============================================================
// 超时模块实现。
//   执行线程:io 线程(connect_thread 处理 timer_fd 事件时调用),
//   与 conn_table 的写方(accept / teardown)同线程  不需要加锁。
// ============================================================

// conn_table 定义在 server.cpp
extern std::unordered_map<session_id, std::shared_ptr<Session>> conn_table;


uint64_t get_now_ms()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}


std::vector<session_id> handle_timeout()
{
    std::vector<session_id> expired;
    uint64_t now = get_now_ms();

    for (auto& kv : conn_table)
    {
        
        Session* sn = kv.second.get();

        // 已登录用户:心跳上线前不做空闲踢
        if (!sn->user_id.empty())
            continue;

        // 未登录连接:登录时限到期 → 回收
        if (sn->deadline_ms != 0 && now >= sn->deadline_ms)
        {
            printf("未登录连接超时,回收 sid=%llu\n", (unsigned long long)sn->sid);
            expired.push_back(sn->sid);
        }
    }
    return expired;
}
