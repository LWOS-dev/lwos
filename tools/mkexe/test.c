/* 裸机 LWP 验证程序: 不使用 libc, 不依赖 CRT 启动代码。 */
#include "../../lib/include/abi.h"

/* 暂按 spec2/40 的入口约定定义; 后续与 resman 共用运行时头。 */
typedef struct {
    DWORD version, size;
    PVOID *abi;
    PVOID *rm;
    int argc;
    char **argv;
} LW_ENV;

PVOID *lw_abi_base;

static const char message[] = "LWP TEST OK\n\r";
/* volatile 保留真实的指针读写, 让测试包含 .data 中的绝对地址重定位。 */
static const char *volatile message_ptr = message;
static volatile DWORD initialized = 0x12345678u;
static volatile DWORD zero_check;

__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != 1 || env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base = env->abi;
    if (!LW_ABI_VALID() || !lw_abi_base[LW_SLOT_CONSOLE_PUTS])
        return -2;

    /* bss 应在每次装载时清零, .data 应保持初值。 */
    if (zero_check != 0 || initialized != 0x12345678u) {
        lw_puts("LWP DATA/BSS FAILED\n\r");
        return -3;
    }
    if (message_ptr != message) {
        lw_puts("LWP POINTER RELOCATION FAILED\n\r");
        return -4;
    }
    zero_check = 1;
    lw_puts(message_ptr);
    return 42; /* 加载器应能接收返回值并回到 monitor。 */
}
