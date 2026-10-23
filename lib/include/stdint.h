#ifndef _LW_STDINT_H
#define _LW_STDINT_H

typedef void VOID;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned long long QWORD;
typedef char CHAR;

typedef void* PVOID;
typedef unsigned char* PBYTE;
typedef unsigned short* PWORD;
typedef unsigned int* PDWORD;
typedef unsigned long long* PQWORD;
typedef char* PCHAR; // 表示固定的缓冲区数据

typedef const void* PCVOID;
typedef const unsigned char* PCBYTE;
typedef const unsigned short* PCWORD;
typedef const unsigned int* PCDWORD;
typedef const unsigned long long* PCQWORD;

/* NUL 结尾字符串的指针别名; 不拥有内存, 不记录长度。
 * STR 是 PSTR 的简写。PCSTR 的 const 修饰字符, 不是指针。
 */
// end with NUL, not actually a class
// just addr spelling
typedef CHAR* PSTR;
typedef const CHAR* PCSTR;
typedef PSTR STR; // 表示笼统的字符串
typedef PSTR* PPSTR;
typedef PCSTR* PPCSTR;

#endif