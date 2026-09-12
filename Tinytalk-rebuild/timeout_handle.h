#pragma once





#include <cstdint>

void handle_timeout();       // io 线程 timer_fd 触发时调用:扫描并回收超时连接
void client_reset_timer();   // 刷新指定连接的最后活跃时间(签名待超时轮次定稿)
uint64_t get_now_ms();