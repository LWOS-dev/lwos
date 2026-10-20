#ifndef _STRING_H
#define _STRING_H

#include "stdint.h"

PSTR strtok(PSTR src, CHAR dec);
PCSTR strtok_const(PCSTR src, CHAR dec);
int strcmp(PCSTR s1, PCSTR s2);
int strncmp(PCSTR s1, PCSTR s2, int n);
unsigned int strlen(PCSTR s);
PSTR strcpy(PSTR dest, PCSTR src);
PSTR strchr(PCSTR str, int c);
PSTR strcat(PSTR dst, PCSTR src);
void skip_ws(PPCSTR p);
int strcspn(PCSTR src, PCSTR reject);

CHAR to_upper(BYTE c);
CHAR to_avail(BYTE c);

#endif