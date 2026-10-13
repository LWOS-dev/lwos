#ifndef _LW_KBD_H
#define _LW_KBD_H

#include "io.h"
#include "abi.h"

void kbd_probe();
int kbd_enable();

enum {
    K_NONE = -1,
    K_UP = 0x100,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_HOME,
    K_END,
    K_PGUP,
    K_PGDN,
    K_INS,
    K_DEL,
    K_F1,
    K_F2,
    K_F3,
    K_F4,
    K_F5,
    K_F6,
    K_F7,
    K_F8,
    K_F9,
    K_F10,
    K_F11,
    K_F12,
    K_PRTSC,
    K_PAUSE,
    K_LGUI,
    K_RGUI,
    K_MENU,
    K_CAPS,
    K_NUM,
    K_SCROLL
};

// shifting keys
enum {
    KM_LSHIFT = 0x01,
    KM_RSHIFT = 0x02,
    KM_LCTRL = 0x04,
    KM_RCTRL = 0x08,
    KM_LALT = 0x10,
    KM_RALT = 0x20,
    KM_SHIFT = KM_LSHIFT | KM_RSHIFT,
    KM_CTRL = KM_LCTRL | KM_RCTRL,
    KM_ALT = KM_LALT | KM_RALT
};

void kbd_reset_state();

int kbd_feed(BYTE sc);
int kbd_poll();
BYTE kbd_mods();
int kbd_bind_source(const LW_KBD_SOURCE *source);
void kbd_wait(void);

#endif
