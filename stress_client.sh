#!/bin/bash
# 压测配置区，按需修改
START_NUM=1       # 起始编号
TOTAL_CLIENTS=200 # 要启动多少个客户端
DELAY_MS=50       # 每启动一个间隔多少毫秒，0=瞬间全开

echo "===== 开始批量启动 $TOTAL_CLIENTS 个客户端 ====="

# 循环创建客户端
for ((i=START_NUM; i < START_NUM + TOTAL_CLIENTS; i++)); do
    # 格式化编号为 0001、0002 四位字符串
    CLIENT_ID=$(printf "%04d" $i)
    # 后台启动客户端，& 代表不阻塞，全部并发运行
    ./client "$CLIENT_ID" &
    echo "已启动客户端 ./client $CLIENT_ID  PID=$!"

    # 间隔延迟，避免瞬间创建上万进程触发系统限制
    if [ $DELAY_MS -gt 0 ]; then
        sleep $(echo "scale=3; $DELAY_MS / 1000" | bc)
    fi
done

echo "===== 全部客户端启动完成 ====="
echo "如需杀死所有压测客户端，执行：pkill -f './client'"
