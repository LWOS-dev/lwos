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

    for (uint32_t i=0; !failed && i<in->header.reloc_count; i++) {
        uint32_t rva=in->relocs[i];
        unsigned char le[4]={rva, rva>>8, rva>>16, rva>>24};
        if (fwrite(le, 1, sizeof(le), fp)!=sizeof(le))
            failed=1;
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

static int file_range(const MkexeBuffer *buf, uint32_t off, uint32_t size) {
    return off<=buf->size && size<=buf->size-off;
}

static SECT32_HDR section_at(const MkexeBuffer *buf, unsigned int index) {
    SECT32_HDR section;
    memcpy(&section, buf->data+p_hdr->e_shoff+(size_t)index*p_hdr->e_shsz,
           sizeof(section));
    return section;
}

static int compare_rva(const void *a, const void *b) {
    uint32_t x=*(const uint32_t *)a, y=*(const uint32_t *)b;
    return (x>y)-(x<y);
}

/* ET_EXEC 已由 ld 完成 S+A / S+A-P 的计算。这里仅记录加载时
 * 还要加 base 的位置 r_offset, 不能再次把符号值加到映像中。 */
static int collect_relocs(const MkexeBuffer *buf, MkexeImage *img) {
    size_t capacity=0;
    for (unsigned int i=0; i<p_hdr->e_shnm; i++) {
        SECT32_HDR relsec=section_at(buf, i);
        if (relsec.sh_type!=SHT_REL && relsec.sh_type!=SHT_RELA)
            continue;
        if (relsec.sh_info>=p_hdr->e_shnm)
            goto invalid;
        SECT32_HDR target=section_at(buf, relsec.sh_info);
        /* 调试节等非装载内容不属于 LWP 映像。 */
        if (!(target.sh_flags&SHF_ALLOC))continue;
        if (relsec.sh_type!=SHT_REL || relsec.sh_ensz!=sizeof(REL32) ||
            relsec.sh_size%sizeof(REL32) ||
            !file_range(buf, relsec.sh_offset, relsec.sh_size) ||
            relsec.sh_link>=p_hdr->e_shnm || target.sh_type==SHT_NOBITS)
            goto invalid;
        SECT32_HDR symsec=section_at(buf, relsec.sh_link);
        if (symsec.sh_type!=SHT_SYMTAB || symsec.sh_ensz!=sizeof(SYM32) ||
            symsec.sh_size%sizeof(SYM32) ||
            !file_range(buf, symsec.sh_offset, symsec.sh_size))
            goto invalid;
        for (size_t off=0; off<relsec.sh_size; off+=sizeof(REL32)) {
            REL32 rel;
            SYM32 sym;
            memcpy(&rel, buf->data+relsec.sh_offset+off, sizeof(rel));
            uint32_t type=rel.r_info&0xff, symidx=rel.r_info>>8;
            if (symidx>=symsec.sh_size/sizeof(SYM32))goto invalid;
            memcpy(&sym, buf->data+symsec.sh_offset+(size_t)symidx*sizeof(sym),
                   sizeof(sym));
            if (type!=R_386_32 && type!=R_386_PC32) {
                fprintf(stderr, "unsupported relocation type %u at %08X\n",
                        type, rel.r_offset);
                return -1;
            }
            if (sym.st_shndx==SHN_UNDEF) {
                fprintf(stderr, "undefined relocation symbol %u\n", symidx);
                return -1;
            }
            if (rel.r_offset<target.sh_addr || target.sh_size<4 ||
                rel.r_offset-target.sh_addr>target.sh_size-4 ||
                img->header.image_size<4 || rel.r_offset>img->header.image_size-4)
                goto invalid;
            int loaded=0;
            for (unsigned int j=0; j<p_hdr->e_phnm; j++) {
                PROG32_HDR prog;
                memcpy(&prog, buf->data+p_hdr->e_phoff+(size_t)j*p_hdr->e_phsz,
                       sizeof(prog));
                if (prog.p_type==PT_LOAD && prog.p_filesz>=4 &&
                    rel.r_offset>=prog.p_va &&
                    rel.r_offset-prog.p_va<=prog.p_filesz-4)
                    loaded=1;
            }
            if (!loaded)goto invalid;
            /* SHN_ABS 是不随映像移动的常量; 对它的 PC32 无法用 +base 表示。 */
            if (sym.st_shndx==SHN_ABS) {
                if (type==R_386_32)continue;
                goto invalid;
            }
            if (sym.st_shndx>=p_hdr->e_shnm)goto invalid;
            SECT32_HDR definition=section_at(buf, sym.st_shndx);
            if (!(definition.sh_flags&SHF_ALLOC))goto invalid;
            if (type==R_386_PC32)continue;
            if (img->header.reloc_count==capacity) {
                size_t next=capacity ? capacity*2 : 64;
                if (next>UINT32_MAX/sizeof(uint32_t))goto invalid;
                uint32_t *items=realloc(img->relocs, next*sizeof(*items));
                if (!items) {
                    fprintf(stderr, "out of memory collecting relocations\n");
                    return -1;
                }
                img->relocs=items;
                capacity=next;
            }
            img->relocs[img->header.reloc_count++]=rel.r_offset;
        }
    }
    if (img->header.reloc_count) {
        qsort(img->relocs, img->header.reloc_count, sizeof(*img->relocs), compare_rva);
        for (uint32_t i=1; i<img->header.reloc_count; i++) {
            if (img->relocs[i]-img->relocs[i-1]<4) {
                fprintf(stderr, "duplicate or overlapping relocation at %08X\n",
                        img->relocs[i]);
                return -1;
            }
        }
        if (img->header.image_size>UINT32_MAX-img->header.image_off)goto invalid;
        img->header.reloc_off=img->header.image_off+img->header.image_size;
        if (img->header.reloc_count>(UINT32_MAX-img->header.reloc_off)/4)
            goto invalid;
    }
    return 0;
invalid:
    fprintf(stderr, "invalid or unsupported ELF relocation\n");
    return -1;
}
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

        if (buf.size<sizeof(*p_hdr) || memcmp(p_hdr->e_id, "\177ELF", 4) ||
            p_hdr->e_id[EI_CLASS]!=1 || p_hdr->e_id[EI_DATA]!=1 ||
            p_hdr->e_type!=ET_EXEC || p_hdr->e_mach!=ISA_X86 ||
            p_hdr->e_phsz!=sizeof(PROG32_HDR) ||
            p_hdr->e_shsz!=sizeof(SECT32_HDR) ||
            !file_range(&buf, p_hdr->e_phoff, (uint32_t)p_hdr->e_phnm*p_hdr->e_phsz) ||
            !file_range(&buf, p_hdr->e_shoff, (uint32_t)p_hdr->e_shnm*p_hdr->e_shsz)) {
            fprintf(stderr, "invalid ELF32/i386 executable\n");
            free(buf.data);
            return 1;
        }

        for (int i=0; i<p_hdr->e_phnm; i++) {
            p_prog=(PPROG32_HDR)(buf.data+p_hdr->e_phoff+p_hdr->e_phsz*i);
            if (p_prog->p_type!=PT_LOAD)continue;
            
            if (p_prog->p_filesz > p_prog->p_memsz ||
                !file_range(&buf, p_prog->p_offset, p_prog->p_filesz) ||
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
        img.header.entry=p_hdr->e_entry;
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

        img.image=(unsigned char*)malloc(image_size ? image_size : 1);
        if (!img.image) {
            free(buf.data);
            return 1;
        }
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

        int failed=collect_relocs(&buf, &img);
        if (!failed) {
            printf("LWP_RELOC_OFF=%08X\n", img.header.reloc_off);
            printf("LWP_RELOC_COUNT=%u\n", img.header.reloc_count);
            failed=write_file(argv[2], &img);
        }
        free(img.relocs);
        free(img.image);
        free(buf.data);
        return failed ? 1 : 0;
    }
    return 1;
}
