#ifndef _LW_TIME_H
#define _LW_TIME_H

#include "resman.h"

/* LWOS elapsed-time API, not calendar time or process CPU time.
 * Requires an attached resman ABI and its running PIT timer. Resolution is
 * about 1 ms; scheduling and waits count toward elapsed time. No RTC needed.
 */
static inline DWORD time_now_ms(void) {
    return rm_uptime_ms();
}

/* Unsigned subtraction handles one counter wrap (~49.7 days), provided the
 * measured interval is shorter than the full 32-bit millisecond range.
 */
static inline DWORD time_elapsed_ms(DWORD started) {
    return time_now_ms()-started;
}

#endif
