#include "memcheck.h"

__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    return memcheck(env,0);
}
