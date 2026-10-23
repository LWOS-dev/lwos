#ifndef _CONVERT_H
#define _CONVERT_H

#include "stdint.h"

BYTE hex_digit(char c);
int hex_parse(PPCSTR p, PDWORD out);
int hex_parse_w(PPCSTR p, PQWORD out);
/* Parse a complete unsigned integer: decimal by default, hexadecimal with
 * an h/H suffix (e.g. 123, 0FFh, ABCH). Leading/trailing spaces and tabs are
 * allowed. Signs, 0x prefixes, invalid digits and overflow are rejected.
 * Return 1 on success, 0 on failure; failure leaves *out unchanged.
 */
int num_parse(PCSTR text, PDWORD out);
int num_parse_w(PCSTR text, PQWORD out);
void hex_xlat(QWORD in, PSTR out, int digits);
void bin_xlat(QWORD in, PSTR out, int digits);

#endif
