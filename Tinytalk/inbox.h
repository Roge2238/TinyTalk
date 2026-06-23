#ifndef INBOX_H
#define INBOX_H

#include "common.h"

extern mutex mtx_box;
extern unordered_map<string, vector<string>> Inbox;

void Inbox_add(const char* target, const char* msg);
void Inbox_send(ClientCtx* client);
//sdvvv
#endif