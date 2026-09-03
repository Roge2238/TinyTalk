

struct Packet
{
    uint8_t type;
    uint32_t body_len;
    char* body;
}


using sendFn = std::function<void(const packet&)>;
using uid = std::string;

struct AccountEntry
{
    Id session_id;
    sendFn fn;
};


enum state_t
{
    STATE_INIT = 0,
    STATE_LOGIN,
    STATE_GAME,
    STATE_CLOSED
};



class Session : public std::enable_shared_from_this<Session>
{
    using Ptr = std::shared_ptr<Session>;
    using WeakPtr = std::weak_ptr<Session>;
    using Id = uint64_t;

    public:
        int fd;
        Id id;
        state_t state;

        //读缓冲区
        char read_buf[MAX_BUF];
        int read_pos;
        std::mutex read_mtx;
        //写缓冲区
        char write_buf[MAX_BUF];
        int write_pos;  
        std::mutex write_mtx;

        explicit Session(int fd, Id user_id) :
            fd(fd), id(user_id), state(STATE_INIT) {}
        
        
        WeakPtr get_weak_ptr(){return weak_from_this();}

        //禁止拷贝构造和赋值运算
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;


}


// 内部拥有 user 的 id 和对应 SendFn的映射 哈希表
class Account_table
{
    std::unordered_map<uid, AccountEntry> send_slot_map;

    void bind_send_fn(uid, session_id, sendFn fn)
    {
        send_slot_map[uid] = {session_id , fn};
    }


    std::optional<sendFn> get_send_fn(uid)
    {
        auto it = send_slot_map.find(uid);
        if(it != send_slot_map.end())
        {
            return it->second.fn;
        }
        return std::nullopt;
    }
    // 加入 删除 send_fn 的逻辑

    void delete_send_fn(uid)
    {
        send_slot_map.erase(uid);
    }

}



















int startup(&u_short port);









