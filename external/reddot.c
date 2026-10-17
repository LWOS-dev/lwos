/* 外部 LWP 程序: BIOS 画点, 系统键盘等待, 恢复文本模式。 */
#include "abi.h"
#include "resman.h"

PVOID *lw_abi_base;

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

    /* AX=0013h: 320x200, 256 色。每次调用均经 trampoline 返回保护模式。 */
    if (lw_tramp_call(0x0013, 0, 0, 0) == 0xffff) {
        lw_gfx_exit();
        return -3;
    }
    /* AH=0Ch, AL=04h (红), BH=0 (页), CX=0 (x), DX=0 (y)。 */
    if (lw_tramp_call(0x0c04, 0, 0, 0) == 0xffff) {
        lw_gfx_exit();
        return -4;
    }
    lw_getk();
    /* INT 10h AX=0003h, 同时复位系统的图形状态并清理文本光标。 */
    lw_gfx_exit();
    return 0;
}
