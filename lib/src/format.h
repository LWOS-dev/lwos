#ifndef _LW_FORMAT_INTERNAL_H
#define _LW_FORMAT_INTERNAL_H
#include "stdarg.h"
/* Writer returns a nonnegative value on success, -1 on error. */
int lw_vformat(int (*write)(int,void *), void *context, const char *format, va_list args);
#endif
