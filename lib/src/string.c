#include "string.h"

// split the string, replace the "dec" to "\0",
// returns next-ptr
PSTR strtok(PSTR src, CHAR dec) {
    PSTR p=src;
    while (*p++) {
        if (*p==dec) {
            *p=0;
            return p+1;
        }
    }
    return p;
}
// split the string and returns next-ptr
// but do not modify
PCSTR strtok_const(PCSTR src, CHAR dec) {
    PCSTR p=src;
    while (*p++) {
        if (*p==dec) {
            return p+1;
        }
    }
    return p;
}
// compare 2 strs, returns diff
int strcmp(PCSTR s1, PCSTR s2) {
	while (*s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return (*(PCBYTE)s1 - *(PCBYTE)--s2);
}
int strncmp(PCSTR s1, PCSTR s2, int n) {
    PCBYTE p1 = (PCBYTE)s1;
    PCBYTE p2 = (PCBYTE)s2;

    while (n-- > 0) {
        BYTE c1 = *p1++;
        BYTE c2 = *p2++;

        if (c1 != c2)
            return (int)c1 - (int)c2;

        if (c1 == 0)
            return 0;
    }
    return 0;
}
// returns length of the src
unsigned int strlen(PCSTR s) {
    PCSTR p = s;
    while (*p)
        p++;
    return (unsigned int)(p - s);
}
// copy strings
PSTR strcpy(PSTR dest, PCSTR src) {
    PSTR ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}
// skip nul characters
void skip_ws(PPCSTR p) {
    while (**p==' '||**p=='\t') {
        (*p)++;
    }
}
// returns distance from string to reject characters
int strcspn(PCSTR src, PCSTR reject) {
    PCSTR p = src;
    int sz = 0;
    int l = strlen(reject);
    while (*p++) {
        for (int i=0; i<l; i++) {
            if (*p == reject[i])return (sz+1);
        }
        sz++;
    }
}

CHAR to_upper(BYTE c) {
    if (c>='a'&&c<='z')return c-0x20;
    return c;
}

CHAR to_avail(BYTE c) {
    if (c>=0x20 && c<=0x7f)return c;
    return '.';
}