#include "mouse.h"
#include "abi.h"
#define DATA 0x60
#define STATUS 0x64
#define OBF 0x01
#define IBF 0x02
#define AUX 0x20
#define BAD 0xc0
#define WAIT_LIMIT 100000
#define QUEUE_SIZE 32
static BYTE packet[3], received, saved_config;
static int active;
static MOUSE_EVENT queue[QUEUE_SIZE];
static unsigned head, tail;

static int write_port(WORD port, BYTE value)
{
    for (int i = 0; i < WAIT_LIMIT; i++) {
        if (!(lw_inb(STATUS) & IBF)) {
            lw_outb(port, value);
            return 0;
        }
    }
    return -1;
}
static int read_reply(int aux, BYTE *value)
{
    for (int i = 0; i < WAIT_LIMIT; i++) {
        BYTE s = lw_inb(STATUS);
        if (!(s & OBF)) continue;
        BYTE d = lw_inb(DATA);
        if (s & BAD) return -1;
        if (!!(s & AUX) == aux) {
            *value = d;
            return 0;
        }
        /* Initialization owns the controller; discard stale data. */
    }
    return -1;
}
static int config_write(BYTE value)
{
    if (write_port(STATUS, 0x60) < 0) return -1;
    return write_port(DATA, value);
}
static int mouse_command(BYTE command)
{
    for (int retry = 0; retry < 3; retry++) {
        BYTE reply;
        if (write_port(STATUS, 0xd4) < 0 || write_port(DATA, command) < 0)
            return -1;
        if (read_reply(1, &reply) < 0) return -1;
        if (reply == 0xfa) return 0;
        if (reply != 0xfe) return -1;
    }
    return -1;
}
int mouse_init(void)
{
    BYTE disabled;
    if (active) return 0;
    received = 0;
    head = tail = 0;
    /* Stop scan codes from interleaving with the controller config reply. */
    if (write_port(STATUS, 0xad) < 0) return -1;
    for (int i = 0; i < 256 && (lw_inb(STATUS) & OBF); i++)
        (void)lw_inb(DATA);
    if (write_port(STATUS, 0x20) < 0 || read_reply(0, &disabled) < 0) {
        (void)write_port(STATUS, 0xae);
        return -1;
    }
    /* Contract: monitor enabled the keyboard before entering resman. */
    saved_config = disabled & (BYTE)~0x10;
    if (write_port(STATUS, 0xa8) < 0 ||
        config_write((disabled | 0x10) & (BYTE)~0x22) < 0 ||
        mouse_command(0xf6) < 0 || mouse_command(0xf4) < 0 ||
        config_write(saved_config & (BYTE)~0x22) < 0) {
        (void)mouse_command(0xf5);
        (void)config_write(saved_config);
        return -1;
    }
    active = 1;
    return 0;
}
void mouse_shutdown(void)
{
    if (!active) return;
    (void)write_port(STATUS, 0xad);
    (void)mouse_command(0xf5);
    for (int i = 0; i < 256 && (lw_inb(STATUS) & OBF); i++)
        (void)lw_inb(DATA);
    (void)config_write(saved_config);
    active = 0;
    received = 0;
    head = tail = 0;
}
void mouse_feed(BYTE data)
{
    if (received == 0 && !(data & 0x08)) return;
    packet[received++] = data;
    if (received != 3) return;
    received = 0;
    MOUSE_EVENT event;
    event.buttons = packet[0] & 7;
    /* On overflow, discard displacement but preserve button changes. */
    event.dx = (packet[0] & 0xc0) ? 0 :
               (int)packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    event.dy = (packet[0] & 0xc0) ? 0 :
               (int)packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    unsigned next = (head + 1) % QUEUE_SIZE;
    if (next == tail) tail = (tail + 1) % QUEUE_SIZE;
    queue[head] = event;
    head = next;
}
void mouse_poll(void)
{
    if (!active) return;
    for (int i = 0; i < 32; i++) {
        BYTE s = lw_inb(STATUS);
        if ((s & (OBF | AUX)) != (OBF | AUX)) return;
        BYTE data = lw_inb(DATA);
        if (s & BAD) received = 0;
        else mouse_feed(data);
    }
}
int mouse_read(MOUSE_EVENT *event)
{
    if (!event || tail == head) return 0;
    *event = queue[tail];
    tail = (tail + 1) % QUEUE_SIZE;
    return 1;
}
