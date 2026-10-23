#ifndef _RESMAN_NET_H
#define _RESMAN_NET_H
#include "../../../lib/include/net.h"
int resman_net_open(PBYTE mac);
int resman_net_recv(PVOID buffer,DWORD capacity,DWORD timeout);
int resman_net_send(PCVOID buffer,DWORD length);
int resman_net_close(void);
int resman_net_info(LW_NET_INFO *info);
void resman_net_task_gone(DWORD id);
int resman_net_timer_tick(DWORD now);
#endif
