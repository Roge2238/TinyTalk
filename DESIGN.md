# TinyTalk 重构设计文档(分层架构)

> 状态:草案 v0.1(2026-09-06),待确认后按文档重排代码
> 范围:仅服务端 `Tinytalk-rebuild/`。客户端 `client.cpp` 与协议**保持字节兼容**,重构期间原版 `Tinytalk/` 始终作为回归对照(能跑 = 回归测试)。
> 决策输入:近期不扩展新功能,先把架构与稳定性做扎实;测试采用轻量门禁(编译 + 冒烟脚本)。

---

## 1. 目标

把"网络收包、业务逻辑、游戏模块"三层分清,让将来加游戏(贪吃蛇)、视频、消息持久化时**不需要再动网络层**。

成功标准(可检验):
1. 网络层不知道"消息内容是什么意思"——它只负责:收帧、按类型交给注册的处理器、把模块给的包发出去、管理连接生死。
2. 业务/游戏模块**拿不到也不持有任何 Session 指针**,它们只持有一个 `sendFn`(发送权柄)。
3. 新增一个模块 = 新增一个文件 + 注册几个 type,不改其他模块。

## 2. 设计原则(每条后面都有血的教训,别破例)

| 编号 | 原则 | 对应现在的痛点 |
|---|---|---|
| R1 | **单一所有权**:每个对象只有一个拥有者,由拥有者决定销毁时机 | Session 既 `enable_shared_from_this` 又 `new`/`delete` 裸管理 → weak 永远为空,发送静默失效 |
| R2 | **生命周期收口**:连接的一生只有两个转移点(bind/unbind),所有退出路径(HUP/ERR/recv=-1/超时)汇聚到同一个 teardown | 重构版下线路径全是空注释,断线用户永远挂在表里 |
| R3 | **单向依赖**:底层绝不 include 上层;模块只见能力(sendFn/回调),不见网络层对象 | 原版游戏线程握着 ClientCtx 裸指针发消息 |
| R4 | **一张表一个写入口**:每张表的增删只在固定的函数里发生 | 登录分散在 type_handler 里连做三件事,中间崩溃就是半登录态 |
| R5 | **协议字节兼容 + 可编译门禁**:每一小步都能编译、能对照原版跑 | 重构目录至今没有编译闭环,在没编译的代码上叠概念 |

## 3. 目标目录结构

```
Tinytalk-rebuild/
├── CMakeLists.txt
├── protocol/
│   └── protocol.h            # 帧格式、type 枚举、常量。client 将来也用它(现阶段手工保持一致)
├── net/
│   ├── session.h/.cpp        # Session:fd、收发队列、deadline、closed 标记(不含业务字段)
│   ├── conn_table.h          # fd → shared_ptr<Session>(网络层唯一会话登记处)
│   └── event_loop.h/.cpp     # epoll 循环:accept/读帧/分发/写/超时/teardown(唯一碰 socket 的地方)
├── app/
│   ├── packet.h              # Packet{type, body 自持有}(不依赖 net)
│   ├── user_registry.h/.cpp  # uid → {session_id, sendFn}:应用层唯一"谁在线"登记处
│   ├── dispatcher.h/.cpp     # 按 type 查注册表,调用模块处理器(现阶段还是 if/else,接口先立好)
│   ├── login_handler.cpp     # type 1 登录(唯一调用 bind 的地方)
│   ├── chat.cpp/.h           # type 2/3:私聊、在线列表、收件箱(Inbox 从原版搬入)
│   └── inbox.h/.cpp          # 离线消息表(纯业务,不碰网络)
└── game/
    ├── game_manager.h/.cpp   # 匹配线程 + 房间 + 玩家表(uid → shared_ptr<Player>)
    ├── player.h              # Player{id, sendFn, 出拳缓冲, updated/in_game/disconnected}
    └── caiquan.cpp           # 猜拳规则,纯函数(落地时可并入 game_manager.cpp,见 3.1)
```

> 注意:这个树表达的是**职责边界,不是文件一一对应的强制规定**。哪些需要 .h、哪些需要 .cpp,按 3.1 的规则定。

### 3.1 .h 和 .cpp 的配对规则

`.h/.cpp` 成对只是习惯,不是规定。C++ 里每个 .cpp 单独编译、最后链接,所以:

- **头文件 = 声明给别人 include**,本身不产出符号。只含枚举/常量/struct 布局/inline/模板的头文件**不需要配 .cpp**(protocol.h、packet.h、player.h、conn_table.h 即此类)。
- **.cpp = 把非 inline 定义编译一次**,给链接器提供符号。有真逻辑的模块(h、event_loop、user_registry、inbox、chat)才需要 .h 声明 + .cpp 实现。
- **只有声明没有实现**的东西不需要专属 .cpp;**只有实现、声明放在别的头文件**(或只被本 .cpp 用、进匿名 namespace)的东西不需要专属 .h。
- 判断流程:① 声明会有第二个 .cpp include 吗?没有 → 不需要头文件;② 有且实现是真逻辑 → 配对;③ 只是数据/常量/几行内联 → 只要 .h;④ 纯接口 → 只有 .h。

依赖方向(main 在最上):

```
            main.cpp(拉起线程、装配)
           /        |          \
     net::EventLoop  app::Dispatcher  game::GameManager
        |              |            /
        |    app::user_registry    /
        |              |          /
   net::Session  ←— sendFn(weak 闭包)— game / chat
        \            /
     protocol/protocol.h(最底层,谁都能 include)
```

规则:
- `net/` 不 include `app/`、`game/`。需要通知"用户下线"时,由 main 把回调注入 EventLoop。
- `game/`、`chat` 只 include `protocol/`、`app/packet.h`,持 `sendFn`,不 include `net/session.h`。
- 线程模型见 §6。

## 4. 会话所有权(Session 的一生)—— R1 的具体化

**决策:Session 用 `shared_ptr` 管理,连接表是唯一长期拥有者。**

```
accept 时:  auto sn = make_shared<Session>(fd, next_sid());   // 同时放入 conn_table[fd]
           epoll 事件里只放裸指针 sn.get()                     // 非拥有,仅 io 线程用
epoll 事件: 拿裸指针处理后,立刻回表里 lock 成 shared_ptr 再碰内部
teardown 时: epoll_del → conn_table.erase(fd) → 引用归零自然析构
```

- `enable_shared_from_this` 保留,`sendFn` 闭包捕获 `weak_ptr<Session>`,发送时 `lock()` —— 这才是它不悬垂的前提(对象必须真的被 shared_ptr 拥有过)。
- 因为析构可能被"正在发送中的模块"短暂延迟(它还持着一份引用),Session 内加 `std::atomic<bool> closed`;`enqueue` 时若 closed 直接丢弃。
- **io 线程内同一批 epoll 事件里,禁止在循环中途销毁 Session**(同一 fd 可能在同一批里出现两次事件,第二个事件会拿到已释放的裸指针)。做法:循环里只把要关的 fd 记下来,批处理结束后统一 teardown。

## 5. 生命周期收口(bind / teardown)—— R2 的具体化

### 5.1 只有两个转移点

```
bind(登录成功, 唯一入口:login_handler):
    1. session->uid = uid; session->deadline 策略改为"已登录"(见 §7)
    2. 查 registry:同 uid 是否已有旧连接?
        有 → 踢旧:把旧 sid 交给 io 线程 teardown(登录处理本身就在 io 线程,直接调)
    3. registry.bind(uid, {sid, sendFn(weak 闭包)})
    4. chat 模块:登录后 flush 收件箱

teardown(session, 唯一出口, 所有路径都调它):
    顺序固定,别改:
    ① epoll_del(fd)             —— 新包不再进来
    ② conn_table.erase(fd)      —— 会话从网络层消失(引用可能被在途发送短暂持有)
    ③ session->closed = true
    ④ 若 uid 非空:registry.unbind(uid)   —— 只删 session_id 匹配的那一项(防旧连接误删新注册)
    ⑤ 通知各模块"用户下线"(main 注入的回调)
        → game: on_user_offline(uid)(擦玩家表/标记对局)
        → chat: 无(收件箱保留,离线消息)
    ⑥ close(fd)
    ⑦ 引用归零,Session 析构
```

触发 teardown 的全部路径(少一条都是 bug):
- recv 返回 0 或错误(io 线程,立即)
- 事件带 EPOLLHUP / EPOLLERR(io 线程,立即)
- 心跳/空闲超时(io 线程,见 §7)
- 同 uid 重复登录踢旧连接(io 线程)
- 服务端退出(进程退出前遍历 conn_table)

### 5.2 表职责清单(写代码前先背下来)

| 表 | 归属层 | key | 存什么 | 写入口(唯一) |
|---|---|---|---|---|
| conn_table | net | fd | shared_ptr\<Session\>(含 uid、deadline、队列) | accept 加 / teardown 删 |
| user_registry | app | uid | {session_id, sendFn} | bind / unbind |
| Inbox | chat 模块 | uid | deque\<string\> | chat 模块自己 |
| player_table | game 模块 | uid | shared_ptr\<Player\> | 模块自己(入队/下线回调) |
| 匹配队列 | game 模块 | — | queue\<uid\>(只存 id 字符串!) | 模块自己 |

要点:
- **同一用户只在一张表里查"在不在线":user_registry。** conn_table 里有不等于在线(可能没登录)。
- 原版的 online_table 与 account_table 并存 → 合并为 registry 一张(它的每项本来就含 sendFn)。
- 游戏模块的玩家表**不是**在线表,它只登记"申请过/正在玩游戏的用户",下线时擦掉即可。

## 6. 线程模型与跨线程通信

```
T_io      : epoll 循环。所有收包、解析、业务 handler、登录/下线、超时,都在这一条线程上串行执行。
             → 应用层状态机(登录态、registry、Inbox)天然无锁。
T_game    : 每个游戏一个匹配/对局线程,由 game 模块自己管理。
             → 只碰 game 模块内部对象 + sendFn。
```

跨线程通道只有两条,方向固定:
- **模块 → 网络**:`sendFn(packet)`。实现 = `weak.lock()` 成功 → 把包 push 进 Session 的发送队列(内部一把短锁,写一个字节计数)。绝不让模块线程碰 fd / epoll。
- **网络 → 模块**:type 分发(在 io 线程同步调用模块 handler)+ 下线回调。模块内部要上自己的锁(因为对局线程也在碰同一份状态,如 Player 的出拳缓冲 —— 沿用原版 `p->mtx` 的做法)。

锁序约定(防死锁,写死):io 线程可以持"模块锁";模块线程**只能**持"模块锁 + Session 发送队列锁"(enqueue 内),绝不允许模块线程反过来等 io 线程做任何事。sendFn 永不阻塞。

推论:原版的 `write_mtx`、`epoll_mod`(在 append_pkg 里被游戏线程调用)这些跨线程碰网络层的代码全部消失 —— io 线程独占所有 socket 操作。

## 7. 超时与心跳

- Session 增加 `deadline_ms`,由 io 线程的 timer_fd 定期扫描 **conn_table**(不是 registry!未登录连接不在 registry,但同样要回收)。
- 未登录连接:10s 无活动直接 teardown(防半连接堆积)。
- **已登录用户:现阶段不要做 10s 空闲断线** —— 客户端没有心跳,聊着天挂机 10 秒就被踢,原版都不会。要么设很长(如 10min),要么等客户端加心跳后再收紧。判定"无活动"= 该 fd 最近一次收包时间。
- 每处理完一个 fd 的事件就刷新它的 deadline(原版注释里 "client_reset_timer" 就是这个意图)。

## 8. 协议与分发(现阶段从简,接口先立)

### 8.1 字节兼容
服务端必须原样吃下现客户端 `client.cpp` 的协议:5 字节头(type + 4 字节大端长度)+ body;type 1~5 语义与 body 格式一律不动(包括 type2 的 `目标id?发送者id: 内容` 这种历史格式)。收件箱、在线列表等聊天行为与原版一致 —— **原版客户端能连上新服务端,是每条验收的硬指标**。

### 8.2 分发接口(现在里面是 if/else,但结构上留出注册位)
网络层解析完帧后只做一件事:按 `session->state` 与 `type` 找到处理器调用。目标形态(等有第二个游戏/模块时再切):

```cpp
// dispatcher.h(设计目标)
struct Handler {
    const char* name;
    // 返回 false 表示协议错误,由 dispatcher 决定是否断开
    bool (*on_packet)(SessionInfo&, const Packet&);
};
void dispatcher_register(int type, Handler h);   // 模块启动时自注册
```

现阶段不引入函数指针表也行,但 **type 的魔法数字必须收进 `protocol/protocol.h` 的枚举**,网络层与模块都 include 它,不再散落 1/2/3/4/5。

### 8.3 状态机
网络层只认两种状态:`PRE_LOGIN`(只放行 type 1)和 `ONLINE`(按 type 分发)。原版的 `STATE_GAME` 删掉 —— 游戏状态属于游戏模块,网络层路由它只会串台。

## 9. 游戏模块设计(迁移原版时按这个写)

针对原版僵尸玩家问题与重构版 weak 队列的混乱,目标设计:

```cpp
// player.h —— Player 不再持有任何 Session/ctx,只有发送权柄
struct Player {
    std::string id;
    sendFn out;                      // 创建时从 registry 拷一份(避免每次发送抢注册表锁)
    GameData data;                   // 出拳缓冲等,受 mtx 保护
    std::atomic<bool> updated, in_game, disconnected;
    std::mutex mtx; std::condition_variable cv;
};

// game_manager.h
class GameManager {
    void on_user_offline(const std::string& uid);   // 下线回调:擦表;对局中只置 disconnected
    void handle_input(const std::string& uid, const Packet&); // type4/5 入口
    void match_loop();                               // 线程
private:
    std::unordered_map<std::string, std::shared_ptr<Player>> player_table;
    std::queue<std::string> match_q;                 // 只存 uid!
    std::mutex mtx; std::condition_variable cv;
};
```

三条规则(取代原版/重构版所有 weak/僵尸/双 push 的绕法):
1. **匹配队列只存 uid 字符串**。出队两个 uid → 在 `player_table` 里查:
   - 查不到(已下线)→ 丢弃,继续;
   - 查得到但 `in_game` → 放回队尾或丢弃;
   - 校验通过 → 在同一把 `mtx` 下标记 `in_game`,开对局线程。
   → 不存在"队列里的僵尸指针",不存在自己匹配到自己(重构版双 push 那种),weak_ptr 整个不需要。
2. **对局线程持有两个 `shared_ptr<Player>` 强引用保活**(原版 come_on_game 的做法是对的,照搬)。对局期间用户掉线 → `on_user_offline` 只擦表并置 `disconnected`;对局线程看见 disconnected 结束对局、通知对手,线程退出自然释放引用。erase 永远炸不到对局。
3. Player 的 `out` 是创建/复用时从 registry 拷的 sendFn;发送不查注册表、不碰 Session。掉线后该闭包 lock 失败 → 静默丢弃,无悬垂。

出拳等游戏内消息:dispatcher 把 type 5 交给 `handle_input`,写入 `p->data` 并置 `updated`,唤醒对局线程 —— 与原版一致。

## 10. 实施里程碑(每步可编译、可对照原版跑)

### M0 门禁与骨架(半天)
- 建目录、写 `CMakeLists.txt`(可执行 `tinytalkd`),main 只起监听。
- `scripts/build.sh`:cmake + make,失败即退出非 0。
- `scripts/smoke.sh`:起新服务端 → 原版客户端×2 登录互发 → 断言收到 → 杀服务端。任何一步失败退出非 0。
- 约定:每提交前跑 build.sh + smoke.sh(轻量门禁,即用户决策)。

### M1 网络层重写(重头,别贪快)
按 §4-§8 落地:conn_table + shared_ptr Session、teardown 收口、超时(未登录 10s / 已登录不断)、user_registry(bind/unbind/踢重登)、type 1/2/3 + Inbox 从原版搬入。
**本里程碑不含游戏**(客户端发 game 请求返回"暂不可用"即可)。
验收:新服务端 + 原版客户端跑通 上线/私聊/离线收件箱/在线列表/掉线清理(在线列表干净、无崩溃);ASAN 构建跑 30 分钟压测(stress_client.sh)无崩溃无泄漏。

### M2 游戏模块迁移
把猜拳按 §9 迁入(uid 队列 + 房间保活 + sendFn),删掉旧 player 逻辑。
验收:双客户端匹配、出拳、胜负、对局中掉线(对手应收到结束通知而不是卡死)、掉线再匹配,行为与原版一致。

### M3(暂缓,等有新功能需求再动)
- 发送队列化(eventfd 唤醒 io 线程,替换写缓冲);当前 write_buf + 每连接写锁在聊天规模下够用,不必提前。
- 注册式 dispatch(§8.2)与多游戏路由(协议里加 game_id)。
- 视频/大包:到那时协议重构,自然落到 protocol.h,与网络层无关 —— 这正是分层的回报。

## 11. 动手前对照清单(现有代码该改的地方)

- [ ] Session:必须存在 make_shared 拥有者;on_login 的 weak 闭包才有意义
- [ ] 删掉/合并 online_table 与 account_table 的职责重叠(§5.2)
- [ ] 补齐下线路径:epoll 循环错误分支 → teardown(现在全是空注释)
- [ ] timeout_handle 改为扫 conn_table,且能处理未登录连接(现在遍历 account_table,类型对不上)
- [ ] Inbox/notify_user/USER_ID_LEN/clients_mtx 等符号搬入或删除(现在引用了但没定义)
- [ ] 游戏匹配重写(§9):现在双 push、对局永不开始的死代码、weak 出队后不 lock 直接用
- [ ] 常量收进 protocol.h,魔法数字清掉
- [ ] 加 CMakeLists + build.sh + smoke.sh,提交前必跑
