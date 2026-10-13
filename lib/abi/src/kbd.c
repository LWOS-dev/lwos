#include "kbd.h"
#include "text.h"

#define KBD_DATA 0x60
#define KBD_STAT 0x64
#define KBD_CMD  0x64

// output buffer full
#define ST_OBF   0x01
// ctrl busy
#define ST_IBF   0x02
#define ST_SYS   0x04
#define ST_A2    0x08
#define ST_INH   0x10
// data from aux not kbd
#define ST_AUXB  0x20  
#define ST_TIMEO 0x40
#define ST_PERR  0x80

static LW_KBD_SOURCE input_source;

int kbd_bind_source(const LW_KBD_SOURCE *source) {
    if (!source || !source->read || !source->wait)return -1;
    input_source=*source;
    return 0;
}

void kbd_wait(void) {
    if (input_source.wait)input_source.wait();
}

static int wait_write() {
    for (int i = 0; i < 100000; i++) {
        if ((inb(KBD_STAT) & ST_IBF) == 0) {
            return 1;
        }
        io_wait();
    }
    return 0;
}

static int wait_read() {
    for (int i = 0; i < 100000; i++) {
        if (inb(KBD_STAT) & ST_OBF) {
            return 1;
        }
        io_wait();
    }
    return 0;
}

void kbd_probe() {
    if (input_source.read)return; /* RESMAN 接管后不再发探测命令。 */
    BYTE s = inb(KBD_STAT);

    puts("KERBOARD PROBE\n\r");

    if (s == 0xff) {
        puts("NO 8042 RESPOND\n\r");
        return;
    }

    if  (s & ST_IBF) {
        puts("CONTROLLER NO COMMAND ACCEPTED\n\r");
        return;
    }

    if (!wait_write()) {
        puts("TIMEOUT WAIT DEVICE\n\r");
        return;
    }
    outb(KBD_CMD, 0xaa);
    if (!wait_read()) {
        puts("NO REPLY\n\r");
        return;
    }
    BYTE r = inb(KBD_DATA);
    if (r == 0x55) {
        puts("PASS\n\r");
    } else {
        puts("UNPASS\n\r");
    }

    if (!wait_write()) {
        puts("TIMEOUT BEFORE READ-CONFIG\n\r");
        return;
    }
    outb(KBD_CMD, 0x20);
    if (!wait_read()) {
        puts("READ CONFIG FAULT\n\r");
        return;
    }

    BYTE c = inb(KBD_DATA);
    puts("  CONFIG BYTE INFO = ");
    put_byte(c);
    puts("H\n\r   IRQ:     ");
    puts(c&0x01?"KBD ":"kbd ");
    puts(c&0x02?"AUX":"aux");
    puts("\n\r  CLOCK:    ");
    puts(c&0x10?"kbd ":"KBD ");
    puts(c&0x20?"aux ":"AUX ");
    puts("\n\r  SET FLAG: ");
    puts(c&0x04?"SYS ":"sys ");
    puts(c&0x40?"XLAT":"xlat");
    puts("\n\r");

    if (c&0x10) {
        puts("KERBOARD CLOCK DISABLED\n\r");
    }
    if ((c&0x40)==0) {
        puts("TRANSLATION OFF\n\r");
    }

    if (!wait_write()) {
        puts("TIMEOUT BEFORE ECHO\n\r");
        return;
    }
    outb(KBD_DATA, 0xEE);
    if (!wait_read()) {
        puts("DEV ECHO NO REPLY\n\r");
        return;
    }

    r = inb(KBD_DATA);
    puts("DEVICE EHCO EEH ->");
    puts(r==0xee?"ATTACHED KEYBOARD\n\r":"!UNEXPECTED\n\r");

    return;
}

static int write_config(BYTE v) {
    if (!wait_write()) {
        return 0;
    }
    outb(KBD_CMD, 0x60);
    if (!wait_write()) {
        return 0;
    }
    outb(KBD_DATA, v);
    return 1;
}
static int read_config(PBYTE out) {
    if (!wait_write()) {
        return 0;
    }
    outb(KBD_CMD, 0x20);
    if (!wait_read()) {
        return 0;
    }
    *out = inb(KBD_DATA);
    return 1;
}

// re enable
int kbd_enable() {
    if (input_source.read)return 1;
    BYTE c;

    if (!read_config(&c)) {
        puts("COULD NOT READ CONFIG BYTE\n\r");
        return 0;
    }
    puts("CONFIG BEFORE ");
    put_byte(c);
    puts("H\n\r");

    if (wait_write()) {
        outb(KBD_CMD, 0xAE);
    }
    if (!read_config(&c)) {
        puts("COULD NOT REREAD CONFIG\n\r");
        return 0;
    }
/*
    if (c&0x10) {
        c&=(BYTE)~0x10;
        c|=0x40;
        if (!write_config(c)) {
            puts("FAILED WRITE CONFIG\n\r");
            return 0;
        }
        if (!read_config(&c)) {
            puts("FAILED VERIFY CONDIG\n\r");
            return 0;
        }
    }*/

    c&=(BYTE)~0x10;
    c|=0x41; /* Set-1 转换和 IRQ1，启动阶段 PIC 仍屏蔽。 */

    if (!write_config(c)) {
        puts("FAILED WRITE CONFIG\n\r");
        return 0;
    }
    if (!read_config(&c)) {
        puts("FAILED VERIFY CONFIG\n\r");
        return 0;
    }

    puts("CONFIG AFTER  ");
    put_byte(c);
    puts("H\n\r");
    if (c&0x10) {
        puts("BIT4 UNRELIABLE, CTN ANYWAY\n\r");
    }

    for (int i = 0; i < 32 && (inb(KBD_STAT) & ST_OBF); i++) {
        (void)inb(KBD_DATA);
    }

    if (!wait_write()) {
        puts("TIMEOUT BEFORE ENABLE-SCANNING\n\r");
        return 0;
    }
    outb(KBD_DATA, 0xF4);
    if (!wait_read()) {
        puts("ENABLE-SCANNING NO ACK\n\r");
        return 0;
    }

    BYTE r = inb(KBD_DATA);
    puts("SCAN ->");
    put_byte(r);
    puts("H\n\r");

    if (r==0xfa) {
        puts("  ACK, LIVE KBD\n\r");
        return 1;
    }
    puts(r==0xfe?"RESEND\n\r":"UNEXPACTED\n\r");
    return 0;
}

static const char map_lo[0x59] = {
	[0x01] = 27,  [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
	[0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b',
	[0x0F] = '\t',
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
	[0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
	[0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
	[0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
	[0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
	[0x28] = '\'',[0x29] = '`', [0x2B] = '\\',
	[0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
	[0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
	[0x37] = '*', [0x39] = ' ',
	/* keypad, numlock on */
	[0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4A] = '-',
	[0x4B] = '4', [0x4C] = '5', [0x4D] = '6', [0x4E] = '+',
	[0x4F] = '1', [0x50] = '2', [0x51] = '3',
	[0x52] = '0', [0x53] = '.',
};
static const char map_hi[0x59] = {
	[0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
	[0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
	[0x0C] = '_', [0x0D] = '+',
	[0x1A] = '{', [0x1B] = '}', [0x27] = ':', [0x28] = '"', [0x29] = '~',
	[0x2B] = '|', [0x33] = '<', [0x34] = '>', [0x35] = '?',
};
static const short map_fn[0x59] = {
	[0x3B] = K_F1, [0x3C] = K_F2, [0x3D] = K_F3,  [0x3E] = K_F4,
	[0x3F] = K_F5, [0x40] = K_F6, [0x41] = K_F7,  [0x42] = K_F8,
	[0x43] = K_F9, [0x44] = K_F10,[0x57] = K_F11, [0x58] = K_F12,
	[0x47] = K_HOME, [0x48] = K_UP,   [0x49] = K_PGUP,
	[0x4B] = K_LEFT, [0x4D] = K_RIGHT,
	[0x4F] = K_END,  [0x50] = K_DOWN, [0x51] = K_PGDN,
	[0x52] = K_INS,  [0x53] = K_DEL,
};
static const short map_e0[0x80] = {
	[0x47] = K_HOME, [0x48] = K_UP,   [0x49] = K_PGUP,
	[0x4B] = K_LEFT, [0x4D] = K_RIGHT,
	[0x4F] = K_END,  [0x50] = K_DOWN, [0x51] = K_PGDN,
	[0x52] = K_INS,  [0x53] = K_DEL,
	[0x37] = K_PRTSC,
	[0x5B] = K_LGUI, [0x5C] = K_RGUI, [0x5D] = K_MENU,
};
static BYTE mods;                     /* KM_* bits held right now */
static BYTE caps, num, scroll;        /* lock states */
static BYTE e0_pending;               /* saw 0xE0: next byte is extended */
static BYTE e1_skip;                  /* Pause bytes still to swallow */

BYTE kbd_mods() { return mods; } // dir

static void set_leds();

void kbd_reset_state() {
    mods = 0;
    e0_pending = 0;
    e1_skip = 0;
    caps = num = scroll = 0;
    set_leds();
}

static void set_leds() {
    BYTE v = (BYTE)((scroll?1:0)|(num?2:0)|(caps?4:0));

    if (input_source.read) {
        if (input_source.set_leds)input_source.set_leds(v);
        return;
    }

    if (!wait_write())return;
    outb(KBD_DATA, 0xed);
    if (!wait_read())return;
    (void)inb(KBD_DATA);
    if (!wait_write())return;
    outb(KBD_DATA, v);
    if (wait_read())(void)inb(KBD_DATA);
}
static void set_mod(BYTE bit, int down) {
    if (down) mods |= bit;
    else      mods &= (BYTE)~bit;
}

static int do_modifier(BYTE sc, int down, int ext, int *handled) {
    *handled = 1;
    switch (sc) {
        case 0x2a: if (!ext)set_mod(KM_LSHIFT, down); return K_NONE;
        case 0x36: set_mod(KM_RSHIFT, down); return K_NONE;
        case 0x1d: set_mod(ext?KM_RCTRL:KM_LCTRL, down); return K_NONE;
        case 0x38: set_mod(ext?KM_RALT:KM_LALT, down); return K_NONE;
        case 0x3a: if (down) { caps=!caps; set_leds(); return K_CAPS; }
                   return K_NONE;
        case 0x45: if (down && !ext) { num=!num; set_leds(); return K_NUM; }
                   return K_NONE;
        case 0x46: if (down) { scroll=!scroll; set_leds(); return K_SCROLL; }
                   return K_NONE;
    }
    *handled = 0;
    return K_NONE;
}

int kbd_feed(BYTE sc) {
    int down, ext, handled, r;

    if (e1_skip) {
        e1_skip--;
        return e1_skip?K_NONE:K_PAUSE;
    }
    if (sc==0xe1) {e1_skip=5;return K_NONE;}
    if (sc==0xe0) {e0_pending=1;return K_NONE;}

    ext = e0_pending;
    e0_pending = 0;
    down = (sc&0x80)==0;
    sc&=0x7f;

    r = do_modifier(sc, down, ext, &handled);
    if (handled) {
        return r;
    }
    if (!down) {
        return K_NONE;
    }
    if (ext) {
        if (sc==0x1c)return '\n';
        if (sc==0x35)return '/';
        return map_e0[sc]?map_e0[sc]:K_NONE;
    }
    if (sc>=0x59)return K_NONE;

    if (sc>=0x47&&sc<=0x53&&sc!=0x4a&&sc!=0x4e) {
        int digits = (num?1:0)^((mods&KM_SHIFT)?1:0);
        if (digits) {
            return map_fn[sc]?map_fn[sc]:K_NONE;
        }
        return map_lo[sc]?map_lo[sc]:K_NONE;
    }

    if (map_fn[sc])return map_fn[sc];

    char c = ((mods&KM_SHIFT)&&map_hi[sc])?map_hi[sc]:map_lo[sc];

    if (!c)return K_NONE;

    if (c>='a'&&c<='z') {
        if (((mods&KM_SHIFT)?1:0)^(caps?1:0))
            c=(char)(c-0x20);
        if (mods&KM_CTRL)
            return c&0x1f;
    } else if (mods&KM_CTRL) {
        switch (c) {
            case '[':return 0x1b;
            case '\\':return 0x1c;
            case ']':return 0x1d;
            case '`':return 0;
            case '-':return 0x1f;
        }
    }
    return (int)(BYTE)c;
}

int kbd_poll() {
    if (input_source.read) {
        BYTE scan;
        while (input_source.read(&scan)) {
            int key=kbd_feed(scan);
            if (key!=K_NONE)return key;
        }
        return K_NONE;
    }
    BYTE s = inb(KBD_STAT);
    BYTE d;

    if ((s&(ST_OBF|ST_AUXB))!=ST_OBF) {
        return K_NONE;
    } else {
        d = inb(KBD_DATA);
        //put_byte(d);
        return kbd_feed(d);
    }
}
