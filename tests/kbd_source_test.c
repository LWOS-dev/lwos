#include <assert.h>
#include "kbd.h"

int getk(void);
char getc(void);

static BYTE queue[64];
static int begin, end, waits, reads;
static BYTE leds;
BYTE inb(WORD port) { (void)port; reads++; return 0; }
void outb(WORD port, BYTE value) { (void)port; (void)value; assert(0); }
static void feed(BYTE scan) { queue[end++]=scan; }
static int read_scan(PBYTE scan)
{
    if (begin==end)return 0;
    *scan=queue[begin++];
    return 1;
}
static void wait_scan(void) { waits++; feed(0x1e); }
static void set_leds(BYTE value) { leds=value; }

int main(void)
{
    LW_KBD_SOURCE source={read_scan,wait_scan,set_leds};
    assert(kbd_bind_source(0)==-1);
    assert(kbd_bind_source(&source)==0);
    source.read=0; /* binder 必须复制结构，不能保留栈上地址。 */
    assert(kbd_poll()==K_NONE);
    assert(getk()=='a' && waits==1);
    feed(0x9e); feed(0x2a); feed(0x1e); feed(0x9e); feed(0xaa);
    assert(kbd_poll()=='A');
    assert(kbd_poll()==K_NONE && kbd_mods()==0);
    feed(0x3a); feed(0xba); feed(0x30);
    assert(kbd_poll()==K_CAPS && leds==4);
    assert(kbd_poll()=='B');
    feed(0x41);
    assert(kbd_poll()==K_F7);
    feed(0xe0); feed(0x48);
    assert(getc()==27 && getc()=='[' && getc()=='A');
    assert(kbd_poll()==K_NONE && reads==0);
    return 0;
}
