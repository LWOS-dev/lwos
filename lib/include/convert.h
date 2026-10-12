#ifndef _CONVERT_H
#define _CONVERT_H

#include "stdint.h"

BYTE hex_digit(char c);
int hex_parse(PPCSTR p, PDWORD out);
void hex_xlat(QWORD in, PSTR out, int digits);

#endif