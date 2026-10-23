#ifndef _ETH_TMP_H
#define _ETH_TMP_H
#include "abi.h"
#include "lwft.h"
typedef LWFT_HDR *PLWFT_HDR;
int eth_init(void);
int eth_receive(void);
void eth_close(void);
#endif
