#ifndef _LW_STDARG_H
#define _LW_STDARG_H
/* GCC builtins, as in lwos/v1b5/include/stdarg.h. */
typedef __builtin_va_list va_list;
#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif
#define va_start(ap,last) __builtin_va_start(ap,last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap,type) __builtin_va_arg(ap,type)
#define va_copy(dst,src) __builtin_va_copy(dst,src)
#endif
