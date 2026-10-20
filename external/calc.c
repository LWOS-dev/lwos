#include "abi.h"
#include "resman.h"

#define TUI_BLACK   0
#define TUI_BLUE    1
#define TUI_GREEN   2
#define TUI_AQUA    3
#define TUI_RED     4
#define TUI_PURPLE  5
#define TUI_YELLOW  6
#define TUI_WHITE   7
#define TUI_LIGHT   8

PVOID *lw_abi_base;
PWORD video = (PWORD)0xb8000;
BYTE vi_attr = 0x07;

void putxy(const char *str, int x, int y) {
    for (int i = 0; str[i]; i++) {
        video[x + y * 80 + i] = ((WORD)vi_attr << 8) | (BYTE)str[i];
    }
}
void set_attr(BYTE fore, BYTE back) {
    vi_attr = fore | (back << 4);
}
void screen_fill(BYTE back) {
    for (int i=0; i<80*25; i++) {
        video[i]=back<<12;
    }
}
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != LW_ENV_VERSION ||
        env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base = env->abi;
    if (!LW_ABI_VALID() || !lw_abi_base[LW_SLOT_TRAMP_CALL] ||
        !lw_abi_base[LW_SLOT_CONSOLE_GETK] ||
        !lw_abi_base[LW_SLOT_IO_VIDEO_EXIT])
        return -2;
    
    screen_fill(TUI_BLACK);
    lw_getk();
    return 0;
}