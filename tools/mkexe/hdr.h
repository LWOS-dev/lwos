#ifndef _HDR_H
#define _HDR_H

#define EI_MAG0         0 // 0x7f
#define EI_MAG1         1 // 'E'
#define EI_MAG2         2 // 'L'
#define EI_MAG3         3 // 'F'
#define EI_CLASS        4 // 1=32 2=64
#define EI_DATA         5 // 1=lit 2=big endian
#define EI_VERSION      6 // 1 cst
#define EI_OSABI        7 // 0=sysv
#define EI_ABIVER       8 // target abi, ignored

#define ET_NONE         0x00 // e_type
#define ET_EXEC         0x02

#define ISA_X86         0x03 // e_mach
#define ISA_AMD64       0x3E

typedef struct _ELF32_HDR {
    unsigned char e_id[16]; // +0h
    unsigned short e_type;  // +10h
    unsigned short e_mach;  // +12h
    unsigned int e_ver;     // +14h
    unsigned int e_entry;   // +18h
    unsigned int e_phoff;   // +1ch; prog hdr table
    unsigned int e_shoff;   // +20h; sect hdr table
    unsigned int e_flags;   // +24h
    unsigned short e_ehsz;  // +28h; elf hdr size=52(34H)
    unsigned short e_phsz;  // +2ah; prog hdr size=32(20H)
    unsigned short e_phnm;  // +2ch; prog hdr entries
    unsigned short e_shsz;  // +2eh; sect hdr size=40(28H)
    unsigned short e_shnm;  // +30h; sect hdr entries
    unsigned short e_shidx; // +32h; sect str index
} ELF32_HDR, *PELF32_HDR;

#define PT_NULL     0
#define PT_LOAD     1
#define PF_X        0x1
#define PF_W        0x2
#define PF_R        0x4

typedef struct _PROG32_HDR {
    unsigned int p_type;    // +0h
    unsigned int p_offset;  // +4h
    unsigned int p_va;      // +8h; virtual address
    unsigned int p_pa;      // +ch; physical address
    unsigned int p_filesz;  // +10h; segment in img sz
    unsigned int p_memsz;   // +14h; segment in mem sz
    unsigned int p_flags;   // +18h; segment-dependent flags
    unsigned int p_align;   // +1ch
} PROG32_HDR, *PPROG32_HDR;

const char sh_type_str[][20] = {
    [0x0] ="SHT_NULL           ",
    [0x1] ="SHT_PROGBITS       ",
    [0x2] ="SHT_SYMTAB         ",
    [0x3] ="SHT_STRTAB         ",
    [0x4] ="SHT_RELA           ",
    [0x5] ="SHT_HASH           ",
    [0x6] ="SHT_DYNAMIC        ",
    [0x7] ="SHT_NOTE           ",
    [0x8] ="SHT_NOBITS         ",
    [0x9] ="SHT_REL            ",
    [0xA] ="SHT_SHLIB          ",
    [0xB] ="SHT_DYNSYM         ",
    [0xE] ="SHT_INIT_ARRAY     ",
    [0xF] ="SHT_FINI_ARRAY     ",
    [0x10]="SHT_PREINIT_ARRAY  ",
    [0x11]="SHT_GROUP          ",
    [0x12]="SHT_SYMTAB_SHNDX   ",
    [0x13]="SHT_NUM            "
};

typedef struct _SECT32_HDR {
    unsigned int sh_name;   // +0h
    unsigned int sh_type;   // +4h
    unsigned int sh_flags;  // +8h
    unsigned int sh_addr;   // +ch
    unsigned int sh_offset; // +10h
    unsigned int sh_size;   // +14h
    unsigned int sh_link;   // +18h
    unsigned int sh_info;   // +1ch
    unsigned int sh_adal;   // +20h
    unsigned int sh_ensz;   // +24h
} SECT32_HDR, *PSECT32_HDR;

#endif