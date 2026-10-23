#ifndef _LW_CONSOLE_H
#define _LW_CONSOLE_H
#include "resman.h"
/* CRT binds this EXE's stdout once. Custom lwp_main entries may bind manually.
 * Output is unbuffered; attributes belong to this EXE's BSS. The terminal's
 * shared cursor lets synchronous child EXEs continue at the current position.
 * No VGA cursor, framebuffer access or shared ABI-global stdout is involved.
 */
int stdio_init(PCLW_ENV env);
int stdio_set_cursor(DWORD x, DWORD y);
void stdio_get_cursor(PDWORD x, PDWORD y);
void stdio_set_attr(BYTE attr);
#endif
