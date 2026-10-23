/* Exercise the actual host sender with a lossy in-memory Ethernet transport. */
#define main lwcon_main
#define sendto test_sendto
#define recvfrom test_recvfrom
#define poll test_poll
#include "../tools/lwcon/lwcon.c"
#undef main
#undef sendto
#undef recvfrom
#undef poll
#include <assert.h>

static const uint8_t target[6]={0x52,0x54,0,1,2,3};
static uint8_t last_frame[ETH_FRAME_LEN],ack_frame[60],saved[4096];
static size_t last_length,saved_length;
static uint32_t last_seq=UINT32_MAX,total,checksum;
static unsigned sends,attempts;
static int drop_all,reject;
ssize_t test_sendto(int fd,const void *bytes,size_t length,int flags,
                   const struct sockaddr *address,socklen_t address_length) {
    (void)fd;(void)flags;(void)address;(void)address_length;
    const uint8_t *frame=bytes;struct lwft_hdr h;
    assert(length>=60 && frame[12]==0x88 && frame[13]==0xb5);
    memcpy(&h,frame+14,sizeof(h));
    uint32_t seq=le32toh(h.seq),arg=le32toh(h.arg);unsigned len=le16toh(h.len);
    assert(le32toh(h.magic)==LWFT_MAGIC && len<=1024 && 30u+len<=length);
    sends++;
    if (seq==last_seq) {
        assert(last_length==length && !memcmp(frame,last_frame,length));attempts++;
    } else {
        assert(last_seq==UINT32_MAX || seq==last_seq+1);
        last_seq=seq;attempts=1;last_length=length;memcpy(last_frame,frame,length);
        if (h.op==OP_PUT) {assert(seq==0);total=arg;saved_length=0;checksum=0xffffffffu;}
        else if (h.op==OP_DATA) {
            assert(arg==saved_length && saved_length+len<=sizeof(saved));
            memcpy(saved+saved_length,frame+30,len);saved_length+=len;
            checksum=lwft_crc32(checksum,frame+30,len);
        } else {assert(h.op==OP_END && !len && arg==(checksum^0xffffffffu) && saved_length==total);}
    }
    memset(ack_frame,0,sizeof(ack_frame));
    memcpy(ack_frame,src_mac,6);memcpy(ack_frame+6,target,6);
    ack_frame[12]=0x88;ack_frame[13]=0xb5;
    struct lwft_hdr ack={htole32(LWFT_MAGIC),reject ? OP_NAK : OP_ACK,0,0,
                        h.seq,htole32(reject ? (uint32_t)-3 : saved_length)};
    memcpy(ack_frame+14,&ack,sizeof(ack));return length;
}
int test_poll(struct pollfd *fds,nfds_t count,int timeout) {
    (void)fds;(void)timeout;assert(count==1);
    return !drop_all && attempts>1; /* Lose the first ACK of every request. */
}
ssize_t test_recvfrom(int fd,void *bytes,size_t length,int flags,
                     struct sockaddr *address,socklen_t *address_length) {
    (void)fd;(void)flags;assert(length>=sizeof(ack_frame));
    struct sockaddr_ll *from=(struct sockaddr_ll*)address;
    memset(from,0,sizeof(*from));*address_length=sizeof(*from);
    memcpy(bytes,ack_frame,sizeof(ack_frame));return sizeof(ack_frame);
}
static void reset(void) {
    last_seq=UINT32_MAX;tx_seq=0;sends=attempts=0;drop_all=reject=0;
    saved_length=0;memset(dst_mac,0xff,6);
}
int main(void) {
    char path[]="/tmp/lwcon-test-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);
    uint8_t input[2051];for (unsigned i=0;i<sizeof(input);i++)input[i]=(uint8_t)(i*31);
    assert(write(fd,input,sizeof(input))==sizeof(input));close(fd);
    reset();assert(put_file(path)==0 && tx_seq==5 && sends==10);
    assert(saved_length==sizeof(input) && !memcmp(saved,input,sizeof(input)));
    assert(truncate(path,0)==0);
    reset();assert(put_file(path)==0 && tx_seq==2 && sends==4 && !saved_length);
    reset();reject=1;assert(put_file(path)==1 && !tx_seq && sends==2);
    reset();drop_all=1;assert(put_file(path)==1 && !tx_seq && sends==8);
    unlink(path);
    puts("PASS: lwcon: byte-exact binary/empty transfers, ACK loss, stable retry sequence, NAK, timeout");
    return 0;
}
