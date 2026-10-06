#include <assert.h>
#include <stdio.h>
#include "mouse.h"
#include "abi.h"

static PVOID abi[0x100];
PVOID *lw_abi_base = abi;
static BYTE config = 0x61, reply[1024], source[1024];
static int begin, end, to_mouse, to_config, resend, busy;
static void enqueue(BYTE d, BYTE s) { reply[end] = d; source[end++] = s; }
static BYTE input(WORD port)
{
    if (port == 0x64) return busy ? 2 : (begin < end ? 1 | source[begin] : 0);
    assert(port == 0x60 && begin < end);
    return reply[begin++];
}
static void output(WORD port, BYTE d)
{
    if (port == 0x64) {
        if (d == 0xad) config |= 0x10;
        else if (d == 0xae) config &= ~0x10;
        else if (d == 0xa8) config &= ~0x20;
        else if (d == 0x20) enqueue(config, 0);
        else if (d == 0x60) to_config = 1;
        else if (d == 0xd4) to_mouse = 1;
        else assert(0);
    } else if (to_config) { config = d; to_config = 0; }
    else {
        assert(to_mouse && (d == 0xf6 || d == 0xf4 || d == 0xf5));
        to_mouse = 0;
        enqueue(resend ? 0xfe : 0xfa, 0x20);
        resend = 0;
    }
}
static void packet(BYTE a, BYTE b, BYTE c)
{ mouse_feed(a); mouse_feed(b); mouse_feed(c); }
int main(void)
{
    abi[LW_SLOT_IO_INB] = (PVOID)input;
    abi[LW_SLOT_IO_OUTB] = (PVOID)output;
    MOUSE_EVENT event;
    busy = 1;
    assert(mouse_init() == -1);
    busy = 0; resend = 1;
    assert(mouse_init() == 0);
    assert(config == 0x41); /* preserve keyboard translation, IRQ and clock */
    mouse_feed(0); mouse_feed(7); /* invalid packet starts */
    mouse_feed(0x09); mouse_feed(200);
    assert(!mouse_read(&event));
    mouse_feed(10);
    assert(mouse_read(&event) && event.dx == 200 && event.dy == 10 && event.buttons == 1);
    packet(0x38, 0, 255);
    assert(mouse_read(&event) && event.dx == -256 && event.dy == -1);
    packet(0xcb, 255, 255);
    assert(mouse_read(&event) && event.dx == 0 && event.dy == 0 && event.buttons == 3);
    enqueue(0x1e, 0); /* keyboard byte must not be consumed */
    int old_begin = begin;
    mouse_poll(); assert(begin == old_begin);
    assert(input(0x60) == 0x1e);
    enqueue(0x08, 0x20); enqueue(5, 0x20); enqueue(6, 0x20);
    mouse_poll();
    assert(mouse_read(&event) && event.dx == 5 && event.dy == 6);
    enqueue(0x08, 0x20); enqueue(1, 0xe0); /* corrupt packet: restart */
    enqueue(0x08, 0x20); enqueue(2, 0x20); enqueue(3, 0x20);
    mouse_poll();
    assert(mouse_read(&event) && event.dx == 2 && event.dy == 3);
    for (int i = 0; i < 40; i++) packet(0x08, i, 0);
    int n = 0;
    while (mouse_read(&event)) n++;
    assert(n == 31 && event.dx == 39);
    mouse_shutdown(); assert(config == 0x61);
    assert(mouse_init() == 0);
    assert(!mouse_read(&event));
    mouse_shutdown();
    puts("PASS: mouse packets, signed motion, overflow, queue, polling, ACK/retry, timeout, restore");
}
