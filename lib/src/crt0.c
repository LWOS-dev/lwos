#include "crt.h"
#include "console.h"

PVOID *lw_abi_base;
PVOID *rm_base;
PCLW_ENV lw_env;

/* i386 cdecl permits main(void) to ignore the argc/argv arguments. */
extern int main(int argc, char **argv);
__attribute__((section(".text.start")))
int lwp_main(PCLW_ENV env) {
    if (!env || env->version!=LW_ENV_VERSION || env->size<sizeof(*env) ||
        !env->abi || !env->rm)return RM_ERR_ARGUMENT;
    lw_abi_base=env->abi;
    rm_base=env->rm;
    lw_env=env;
    int rc=stdio_init(env);
    if (rc!=RM_OK)return rc;
    /* Unbuffered output is already in the window. Return through resman so
     * main's exit code and the last screen/caption remain until window close.
     */
    return main(env->argc,env->argv);
}
