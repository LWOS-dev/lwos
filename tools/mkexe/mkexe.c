/* 亲手实现的入口。AI 生成的旧框架保存在 mkexe_reference.c。 */
#include "mkexe.h"
#include "hdr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int read_file(const char* path, MkexeBuffer* out) {
    FILE* fp = fopen(path, "rb");
    long length;
    if (!fp) {
        perror(path);
        return -1;
    }
    if (fseek(fp,0,SEEK_END)||(length=ftell(fp))<0||fseek(fp,0,SEEK_SET)) {
        fprintf(stderr, "cannot measure %s\n",path);
        fclose(fp);
        return -1;
    }
    out->data=malloc(length?length:1);
    if (!out->data) {
        fprintf(stderr, "out of mem\n");
        fclose(fp);
        return -1;
    }
    out->size=length;
    int status=fread(out->data,1,out->size,fp)!=out->size;
    if (fclose(fp))status=1;
    if (status) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    return 0;
}
int write_file(const char* path, MkexeImage* in) {
    FILE* fp=fopen(path, "wb");
    if (!fp) {
        perror(path);
        return 1;
    }
    int failed=0;
    if (fwrite(&in->header, 1, sizeof(in->header), fp)
        != sizeof(in->header)) {
        failed = 1;
    }
    if (!failed &&
    fwrite(in->image, 1, in->header.image_size, fp)
        != in->header.image_size) {
        failed = 1;
    }

    if (fclose(fp) != 0)
        failed = 1;

    return failed ? -1 : 0;
}
static const char _hex_digit[]="0123456789ABCDEF";
void out_dword(void* p) {
    unsigned int v=*(unsigned int*)(p);
    for (int i=0;i<8;i++) {
        printf("%c", _hex_digit[(v>>((7-i)*4))&0xf]);
    }
}
static PELF32_HDR p_hdr;
static PPROG32_HDR p_prog;
static PSECT32_HDR p_sect;
static PSECT32_HDR p_nmse;
const char ph_str[][8] = {
    "PT_NULL",
    "PT_LOAD"
};
int main(int argc, char **argv) {
    MkexeBuffer buf={0};
    MkexeImage img={0};
    if (argc==2 && (!strcmp(argv[1],"-h") || !strcmp(argv[1], "--help"))) {
        printf("usage:\n");
        printf("mkexe input.elf output.exe\n");
        printf("mkexe -d file.exe\n");
        return 0;
    }
    if (argc!=3||argv[2][0]=='-'||
        (argv[1][0]=='-'&&strcmp(argv[1],"-d"))) {
        return 2;
    }
    int dump=!strcmp(argv[1],"-d");
    printf("path_in =%s\n",argv[dump?2:1]);

    unsigned int image_size=0, mem_size=0;

    if (read_file(argv[dump?2:1],&buf))return 1;
    if (dump) {
        p_hdr=(PELF32_HDR)(buf.data);
        printf("E_ENTRY\t%08X\n", p_hdr->e_entry);
        printf("E_PHOFF\t%08X\n", p_hdr->e_phoff);
        printf("E_SHOFF\t%08X\n", p_hdr->e_shoff);
        printf("E_EHSZ\t%04X\n", p_hdr->e_ehsz);
        printf("E_PHSZ\t%04X\n", p_hdr->e_phsz);
        printf("E_PHNM\t%04X\n", p_hdr->e_phnm);
        printf("E_SHSZ\t%04X\n", p_hdr->e_shsz);
        printf("E_SHNM\t%04X\n", p_hdr->e_shnm);
        printf("E_STRX\t%04X\n", p_hdr->e_shidx);

        p_nmse=(PSECT32_HDR)(buf.data+p_hdr->e_shoff+p_hdr->e_shidx*p_hdr->e_shsz);

        if (p_hdr->e_type != ET_EXEC) {
            fprintf(stderr, "Unsupport type of %04X\n", p_hdr->e_type);
            return 1;
        } else {
            printf("Supported type ET_EXEC\n\n");
        }
        printf(" PROGRAM HEADERS\n");
        printf("IDX TYPE    OFFSET    FILE_SZ   VA        PA        MEM_SZ    F\n");
        for (int i=0; i<p_hdr->e_phnm; i++) {
            p_prog = (PPROG32_HDR)(buf.data + p_hdr->e_phoff + i*p_hdr->e_phsz);
            printf("%02X  %s %08X  %08X  %08X  %08X  %08x  %c%c%c\n", i, 
                ph_str[p_prog->p_type],
                p_prog->p_offset,
                p_prog->p_filesz,
                p_prog->p_va,
                p_prog->p_pa,
                p_prog->p_memsz,
                (p_prog->p_flags&PF_R)?'R':' ',
                (p_prog->p_flags&PF_W)?'W':' ',
                (p_prog->p_flags&PF_X)?'X':' '
            );
        }

        printf("\n SECTION HEADERS\n");
        printf("IDX TYPE                F     ADDR      OFFSET    SIZE      NAME\n");
        for (int i=0; i<p_hdr->e_shnm; i++) {
            p_sect = (PSECT32_HDR)(buf.data + p_hdr->e_shoff + i*p_hdr->e_shsz);
            printf("%02X  %s %04X  %08X  %08X  %08X %s\t\n", i,
                sh_type_str[p_sect->sh_type],
                p_sect->sh_flags,
                p_sect->sh_addr,
                p_sect->sh_offset,
                p_sect->sh_size,
                (const char*)(buf.data+p_nmse->sh_offset+p_sect->sh_name)
            );
        }
        return 0;
    } else {
        printf("path_out=%s\n", argv[2]);
        printf(" Refactory mode\n");
        p_hdr=(PELF32_HDR)(buf.data);

        for (int i=0; i<p_hdr->e_phnm; i++) {
            p_prog=(PPROG32_HDR)(buf.data+p_hdr->e_phoff+p_hdr->e_phsz*i);
            if (p_prog->p_type!=PT_LOAD)continue;
            
            if (p_prog->p_filesz > p_prog->p_memsz ||
                p_prog->p_va > UINT32_MAX - p_prog->p_memsz) {
                fprintf(stderr, "invalid PT_LOAD\n");
                return 1;
            }
            if (p_prog->p_filesz != 0) {
                uint32_t file_end = p_prog->p_va + p_prog->p_filesz;
                if (file_end > image_size)
                    image_size = file_end;
            }

            uint32_t mem_end = p_prog->p_va + p_prog->p_memsz;
            if (mem_end > mem_size)
                mem_size = mem_end;
        }

        img.header.magic=LWP_MAGIC;
        img.header.version=LWP_VERSION;
        img.header.hdr_size=sizeof(img.header);
        img.header.entry=0;
        img.header.image_off=0x40;
        img.header.image_size=image_size;
        img.header.mem_size=mem_size;
        img.header.stack_size=0x10000;
        img.header.reloc_off=0;
        img.header.reloc_count=0;
        img.header.sym_off=0;
        img.header.sym_count=0;
        img.header.export_off=0;
        img.header.export_count=0;
        img.header.crc32=0;

        printf("LWP_MAGIC=%08X\n", img.header.magic);
        printf("LWP_VERSION=%04X\n", img.header.version);
        printf("LWP_IMAGE_SIZE=%08X\n", img.header.image_size);
        printf("LWP_MEM_SIZE=%08X\n", img.header.mem_size);

        img.image=(unsigned char*)malloc(image_size);
        memset(img.image,0,image_size);

        for (int i=0; i<p_hdr->e_phnm; i++) {
            p_prog=(PPROG32_HDR)(buf.data+p_hdr->e_phoff+p_hdr->e_phsz*i);
            if (p_prog->p_type!=PT_LOAD)continue;
            memcpy(
                img.image+p_prog->p_va,
                buf.data+p_prog->p_offset,
                p_prog->p_filesz
            );
        }

        if (write_file(argv[2], &img)) {
            return 1;
        } else {
            return 0;
        }

        return 0;
    }
    return 1;
}
