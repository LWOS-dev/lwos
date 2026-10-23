#ifndef _LW_LWFT_H
#define _LW_LWFT_H
/* Shared i386/Linux wire format: Ethernet type is big endian; this header is
 * little endian. No network addresses, IP stack or remote memory commands.
 */
#define LW_ETHERTYPE 0x88b5
#define LWFT_MAGIC 0x5446574cu
#define LWFT_MAX_DATA 1024u
enum {
    OP_HELLO=1, OP_PUT, OP_DATA, OP_END, OP_ACK, OP_NAK, OP_RUN, OP_TEXT
};
typedef struct lwft_hdr {
    unsigned int magic;
    unsigned char op,flags;
    unsigned short len;
    unsigned int seq,arg;
} __attribute__((packed)) LWFT_HDR;
_Static_assert(sizeof(LWFT_HDR)==16,"LWFT wire header");
static inline unsigned int lwft_crc32(unsigned int crc,const void *data,unsigned int len) {
    const unsigned char *p=data;
    while (len--) {
        crc^=*p++;
        for (unsigned i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return crc;
}
#endif
