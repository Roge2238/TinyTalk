#include "inbox.h"
#include "server.h"


mutex mtx_box;
unordered_map<string, vector<string>> Inbox;


// 添加消息到Inbox
void Inbox_add(const char* target, const char* msg) {
    lock_guard<mutex> lk(mtx_box);
    Inbox[target].push_back(msg);
}

// 发送收件箱消息给客户端
void Inbox_send(ClientCtx* client) {

    lock_guard<mutex> lk1(mtx_box);
    
    auto it = Inbox.find(client->user_id);
    if (it == Inbox.end() || it->second.empty()) return;
    vector<string>& vec = it->second;
    
    for (const string& s : vec) {
        append_pkg(client, 3, s.c_str(), s.size());
    }
    vec.clear();
}//ddddd fff