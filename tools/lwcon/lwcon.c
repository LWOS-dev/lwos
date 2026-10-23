#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <endian.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../../lib/include/lwft.h"

static int sock=-1;
static int ifindex;
static uint8_t src_mac[6];
static uint8_t dst_mac[6]={0xff,0xff,0xff,0xff,0xff,0xff};
static uint32_t tx_seq;

static void net_open(const char *ifname) {
    struct ifreq ifr;
    if (!ifname || !*ifname || strlen(ifname)>=IFNAMSIZ) {
        fprintf(stderr,"lwcon: invalid interface name (expected 1..%u characters)\n",IFNAMSIZ-1);
        exit(1);
    }

    // AF_PACKET == LAYER 2
    // SOCK_RAW  == RAW FRAME FROM ETHER
    sock=socket(AF_PACKET,SOCK_RAW,htons(0x88b5));
    if (sock<0) {
        int error=errno;
        fprintf(stderr,"lwcon: socket(AF_PACKET): %s\n",strerror(error));
        if (error==EPERM || error==EACCES)
            fprintf(stderr,"Run with sudo or CAP_NET_RAW.\n");
        exit(1);
    }

    memset(&ifr,0,sizeof ifr);
    snprintf(
        ifr.ifr_ifrn.ifrn_name,
        IFNAMSIZ,
        "%s", ifname
    );
    if (ioctl(sock, SIOCGIFINDEX, &ifr)<0) {
        int error=errno;
        fprintf(stderr,"lwcon: interface '%s': SIOCGIFINDEX: %s\n",ifname,strerror(error));
        if (error==ENODEV || error==ENXIO)
            fprintf(stderr,"Use 'ip -br link' to find the actual interface connected to the target.\n");
        close(sock);
        exit(1);
    }
    ifindex=ifr.ifr_ifru.ifru_ivalue;
    if (ioctl(sock, SIOCGIFHWADDR, &ifr)<0) {
        fprintf(stderr,"lwcon: interface '%s': SIOCGIFHWADDR: %s\n",ifname,strerror(errno));
        close(sock);
        exit(1);
    }
    memcpy(
        src_mac,
        ifr.ifr_ifru.ifru_hwaddr.sa_data,
        6
    );

    struct sockaddr_ll sll = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(0x88b5),
        .sll_ifindex = ifindex,
    };
    if (bind(sock, (struct sockaddr *)&sll,sizeof sll)<0) {
        fprintf(stderr,"lwcon: bind interface '%s': %s\n",ifname,strerror(errno));
        close(sock);
        exit(1);
    }
    printf("lwcon: %s index=%d MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
           ifname,ifindex,src_mac[0],src_mac[1],src_mac[2],src_mac[3],src_mac[4],src_mac[5]);
}

static int net_send(
    uint8_t op,
    uint32_t arg,
    const void *data,
    size_t len
) {
    uint8_t frame[ETH_FRAME_LEN];
    size_t n=0;
    if (len>1024)len=1024;

    memcpy(frame+0,dst_mac,6);
    memcpy(frame+6,src_mac,6);
    frame[12]=0x88;
    frame[13]=0xb5;
    n=14;
    
    struct lwft_hdr h=
    {
        .magic=htole32(LWFT_MAGIC),
        .op=op,
        .flags=0,
        .len=htole16((uint16_t)len),
        .seq=htole32(tx_seq),
        .arg=htole32(arg),
    };

    memcpy(frame+n,&h,sizeof h);
    n+=sizeof h;

    if (len)memcpy(frame+n,data,len);
    n+=len;

    if (n<60) {
        memset(frame+n,0,60-n);
        n=60;
    }

    struct sockaddr_ll to = {
        .sll_family=AF_PACKET,
        .sll_ifindex=ifindex,
        .sll_halen=6,
    };
    memcpy(to.sll_addr,dst_mac,6);


    if (sendto(sock,frame,n,0,(struct sockaddr *)&to,sizeof to)<0) {
        fprintf(stderr,"lwcon: sendto: %s\n",strerror(errno));
        return -1;
    }
    return 0;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
/* Retransmit the identical sequence, never advance until its ACK arrives. */
static int request(uint8_t op,uint32_t arg,const void *data,size_t len,uint32_t accepted) {
    unsigned frames=0,outgoing=0,short_frame=0,wrong_mac=0,bad_header=0,wrong_seq=0,wrong_op=0,wrong_count=0;
    struct lwft_hdr last={0};
    for (unsigned retry=0;retry<8;retry++) {
        if (net_send(op,arg,data,len))return -1;
        uint64_t deadline=now_ms()+500;
        for (;;) {
            uint64_t now=now_ms();
            if (now>=deadline)break;
            struct pollfd pfd={.fd=sock,.events=POLLIN};
            int ready=poll(&pfd,1,(int)(deadline-now));
            if (ready<0) {if (errno==EINTR)continue;perror("poll");return -1;}
            if (!ready)break;
            uint8_t frame[ETH_FRAME_LEN];
            struct sockaddr_ll from;socklen_t from_len=sizeof(from);
            ssize_t n=recvfrom(sock,frame,sizeof(frame),0,(struct sockaddr*)&from,&from_len);
            if (n<0) {if (errno==EINTR)continue;perror("recvfrom");return -1;}
            frames++;
            if (from.sll_pkttype==PACKET_OUTGOING) {outgoing++;continue;}
            if (n<30) {short_frame++;continue;}
            if (frame[12]!=0x88 || frame[13]!=0xb5 || memcmp(frame,src_mac,6) ||
                !memcmp(frame+6,src_mac,6) || (frame[6]&1) ||
                (dst_mac[0]!=0xff && memcmp(frame+6,dst_mac,6))) {wrong_mac++;continue;}
            struct lwft_hdr h;memcpy(&h,frame+14,sizeof(h));
            last=h;
            if (le32toh(h.magic)!=LWFT_MAGIC || h.flags || h.len) {bad_header++;continue;}
            if (le32toh(h.seq)!=tx_seq) {wrong_seq++;continue;}
            if (h.op==OP_NAK) {
                fprintf(stderr,"receiver rejected seq %u: %d\n",tx_seq,(int32_t)le32toh(h.arg));
                return -1;
            }
            if (h.op!=OP_ACK) {wrong_op++;continue;}
            if (le32toh(h.arg)!=accepted) {wrong_count++;continue;}
            memcpy(dst_mac,frame+6,6);tx_seq++;return 0;
        }
    }
    fprintf(stderr,"timeout waiting for ACK seq %u (op=%u, 8 attempts); no valid ACK received\n",tx_seq,op);
    fprintf(stderr,"RX frames=%u outgoing=%u short=%u wrong-MAC/type=%u bad-header=%u wrong-seq=%u wrong-op=%u wrong-count=%u\n",
            frames,outgoing,short_frame,wrong_mac,bad_header,wrong_seq,wrong_op,wrong_count);
    if (bad_header || wrong_seq || wrong_op || wrong_count)
        fprintf(stderr,"last LWFT: magic=%08x op=%u flags=%u len=%u seq=%u arg=%u\n",
                le32toh(last.magic),last.op,last.flags,le16toh(last.len),le32toh(last.seq),le32toh(last.arg));
    fprintf(stderr,"Check target PUT/reply/TX counters; capture with: sudo tcpdump -ni IFACE -e -XX ether proto 0x88b5\n");
    return -1;
}
static int put_file(const char *path) {
    FILE *file=fopen(path,"rb");
    if (!file) {perror(path);return 1;}
    struct stat st;
    if (fstat(fileno(file),&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>UINT32_MAX) {
        fprintf(stderr,"put requires a regular file of at most 4 GiB - 1 byte\n");fclose(file);return 1;
    }
    const char *name=strrchr(path,'/');name=name ? name+1 : path;
    if (!*name || strlen(name)>255) {fprintf(stderr,"invalid filename\n");fclose(file);return 1;}
    int rc=1;
    uint32_t size=(uint32_t)st.st_size,offset=0,crc=0xffffffffu;
    uint8_t data[LWFT_MAX_DATA];
    if (request(OP_PUT,size,name,strlen(name),0))goto done;
    while (offset<size) {
        size_t wanted=size-offset;
        if (wanted>sizeof(data))wanted=sizeof(data);
        if (fread(data,1,wanted,file)!=wanted) {fprintf(stderr,"read failed or file changed\n");goto done;}
        if (request(OP_DATA,offset,data,wanted,offset+(uint32_t)wanted))goto done;
        crc=lwft_crc32(crc,data,wanted);offset+=(uint32_t)wanted;
    }
    if (request(OP_END,crc^0xffffffffu,NULL,0,size))goto done;
    printf("sent %u bytes, CRC32 %08x\n",size,crc^0xffffffffu);rc=0;
done:
    fclose(file);return rc;
}

int main(int argc, char **argv) {
    const char *fixed=NULL;
    if (argc<2||argv[1][0]=='-') {
        fprintf(stderr,"Usage: lwcon IFACE put FILE | lwcon IFACE -s COMMAND\n");
        return 2;
    }
    const char *ifname=argv[1];
    if (argc==4 && !strcmp(argv[2],"put")) {
        net_open(ifname);
        int rc=put_file(argv[3]);close(sock);return rc;
    }
    int opt;
    while ((opt=getopt(argc,argv,"s:"))!=-1) {
        switch (opt) {
            case 's': {
                fixed=optarg;
                break;
            }
            default:return 2;
        }
    }

    if (!fixed) {fprintf(stderr,"Usage: lwcon IFACE put FILE | lwcon IFACE -s COMMAND\n");return 2;}
    printf("net open %s\n", ifname);
    net_open(ifname);

    if (fixed) {
        int rc=net_send(OP_RUN,0,fixed,strlen(fixed));
        close(sock);return rc ? 1 : 0;
    } else {
        printf("NO CONTENT\n");
    }

    close(sock);

    return 0;
}
