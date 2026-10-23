#ifndef _LW_PATH_H
#define _LW_PATH_H
#include "resman.h"

/* Lexical absolute path; collapse separators, . and .. before FAT lookup. */
static inline int lw_path_resolve(PCSTR cwd,PCSTR input,PSTR output,unsigned capacity) {
    char joined[512];unsigned n=0;
    if (!cwd || !input || !output || capacity<2)return RM_ERR_ARGUMENT;
    if (*input!='/') {
        while (*cwd) {
            if (n>=sizeof(joined)-2)return RM_ERR_ARGUMENT;
            joined[n++]=*cwd++;
        }
        joined[n++]='/';
    }
    while (*input) {
        if (n>=sizeof(joined)-1)return RM_ERR_ARGUMENT;
        joined[n++]=*input++;
    }
    joined[n]=0;unsigned length=1;output[0]='/';
    PCSTR p=joined;
    while (*p) {
        while (*p=='/')p++;
        PCSTR start=p;
        while (*p && *p!='/')p++;
        unsigned count=p-start;
        if (!count || (count==1 && start[0]=='.'))continue;
        if (count==2 && start[0]=='.' && start[1]=='.') {
            while (length>1 && output[length-1]!='/')length--;
            if (length>1)length--;
            continue;
        }
        if (length>1) {
            if (length>=capacity-1)return RM_ERR_ARGUMENT;
            output[length++]='/';
        }
        if (count>capacity-1-length)return RM_ERR_ARGUMENT;
        while (count--)output[length++]=*start++;
    }
    output[length]=0;return 0;
}
#endif
