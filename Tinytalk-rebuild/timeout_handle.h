#pragma once
// ============================================================
// timeout_handle.h —— 超时模块(半成品)
// 本轮只把它修到"能被 include、声明不自相矛盾";
// 完整逻辑在超时模块轮次重写(见 DESIGN.md §7)。
// ============================================================
// TODO(R2 超时模块):
//   - handle_timeout 的扫描对象应从 account_table 改为网络层的 conn_table
//     (未登录连接也要能超时回收);
//   - client_reset_timer 的签名/触发点待定稿(在 io 线程收到数据时刷新 deadline)。
#include <cstdint>

void handle_timeout();       // io 线程 timer_fd 触发时调用:扫描并回收超时连接
void client_reset_timer();   // 刷新指定连接的最后活跃时间(签名待超时轮次定稿)
