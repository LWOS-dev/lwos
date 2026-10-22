/* Disposable QEMU guest probe. TIMER_TICKS_ADDRESS comes from resman.elf;
 * only this test touches private tick state, to exercise deadline rollover.
 */
#include "resman.h"
#include "task.h"
PVOID *rm_base;
#define SAMPLE_COUNT 32
struct {
    volatile DWORD stage;
    int error;
    QWORD calibration_cycles;
    QWORD samples[3][SAMPLE_COUNT];
    DWORD tick_deltas[3][SAMPLE_COUNT];
    DWORD uptime_delta;
} results;
static volatile DWORD *const ticks=(volatile DWORD *)TIMER_TICKS_ADDRESS;
static QWORD stamp(void) {
    DWORD lo,hi;
    asm volatile("lfence; rdtsc":"=a"(lo),"=d"(hi)::"memory");
    return ((QWORD)hi<<32)|lo;
}
static int check(int condition, int code) {
    if (condition)return 1;
    results.error=code; results.stage=99; return 0;
}
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || !env->rm)return -1;
    rm_base=env->rm;
    if (!check(rm_sleep(RM_SLEEP_MAX_MS+1)==RM_ERR_ARGUMENT,1))return -1;
    DWORD flags;
    asm volatile("pushfl; popl %0; cli":"=r"(flags)::"memory");
    int rc=rm_sleep(1);
    asm volatile("pushl %0; popfl"::"r"(flags):"memory","cc");
    if (!check(rc==RM_ERR_NOT_READY,2))return -2;
    if (!check(rm_sleep(0)==RM_OK,3))return -3;
    results.stage=1;
    DWORD before=rm_uptime_ms();
    if (!check(rm_sleep(300)==RM_OK,4))return -4;
    results.uptime_delta=rm_uptime_ms()-before;
    if (!check(results.uptime_delta>=300,5))return -5;
    /* Calibrate invariant TSC against 100 actual PIT periods. Busy waiting
     * here is measurement only; rm_sleep itself blocks via the scheduler.
     */
    DWORD edge=*ticks;
    while (*ticks==edge)asm volatile("pause");
    edge=*ticks;
    QWORD start=stamp();
    while ((DWORD)(*ticks-edge)<100)asm volatile("pause");
    results.calibration_cycles=stamp()-start;
    results.stage=2;
    const DWORD durations[3]={1,5,10};
    for (unsigned group=0;group<3;group++)
        for (unsigned i=0;i<SAMPLE_COUNT;i++) {
            /* Vary the call phase inside a PIT period. */
            QWORD phase=stamp();
            QWORD delay=(results.calibration_cycles/1000)*(i%10);
            while (stamp()-phase<delay)asm volatile("pause");
            DWORD t=*ticks;
            start=stamp();
            rc=rm_sleep(durations[group]);
            results.samples[group][i]=stamp()-start;
            results.tick_deltas[group][i]=*ticks-t;
            if (!check(rc==RM_OK && results.tick_deltas[group][i]>=durations[group]+1,6))
                return -6;
        }
    results.stage=3;
    asm volatile("cli":::"memory");
    *ticks=0xfffffffeu;
    asm volatile("sti":::"memory");
    if (!check(rm_sleep(5)==RM_OK && *ticks<100,7))return -7;
    results.stage=4;
    return 42;
}
