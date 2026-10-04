#include "mkexe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned get16(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static int range_ok(size_t size, uint32_t off, uint32_t count, unsigned stride) {
    return off <= size && count <= (size - off) / stride;
}

int mkexe_read_file(const char *path, MkexeBuffer *out) {
    FILE *fp = fopen(path, "rb");
    long length;
    if (!fp) {
        perror(path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) || (length = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET)) {
        fprintf(stderr, "mkexe: cannot measure %s\n", path);
        fclose(fp);
        return -1;
    }
    out->data = malloc(length ? (size_t)length : 1);
    if (!out->data) {
        fprintf(stderr, "mkexe: out of memory\n");
        fclose(fp);
        return -1;
    }
    out->size = (size_t)length;
    int failed = fread(out->data, 1, out->size, fp) != out->size;
    if (fclose(fp)) failed = 1;
    if (failed) {
        fprintf(stderr, "mkexe: cannot read %s\n", path);
        mkexe_buffer_free(out);
        return -1;
    }
    return 0;
}

void mkexe_buffer_free(MkexeBuffer *buffer) {
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

int mkexe_dump(const MkexeBuffer *file) {
    static const char *names[] = {
        "flags", "abi_need", "entry", "image_off", "image_size", "mem_size",
        "stack_size", "reloc_off", "reloc_count", "sym_off", "sym_count",
        "export_off", "export_count", "crc32"
    };
    const unsigned char *p = file->data;
    if (file->size < LWP_HEADER_SIZE || get32(p) != LWP_MAGIC ||
        get16(p + 4) != LWP_VERSION || get16(p + 6) != LWP_HEADER_SIZE) {
        fprintf(stderr, "mkexe: invalid or unsupported LWP header\n");
        return -1;
    }
    uint32_t image_off = get32(p + 0x14), image_size = get32(p + 0x18);
    uint32_t reloc_off = get32(p + 0x24), reloc_count = get32(p + 0x28);
    if (image_off < LWP_HEADER_SIZE || !image_size ||
        !range_ok(file->size, image_off, image_size, 1) ||
        get32(p + 0x1c) < image_size || get32(p + 0x10) >= image_size ||
        (reloc_count && (reloc_off < LWP_HEADER_SIZE ||
                         !range_ok(file->size, reloc_off, reloc_count, 4)))) {
        fprintf(stderr, "mkexe: invalid image or relocation range\n");
        return -1;
    }
    printf("LWP v%u, header=%u, file=%zu bytes\n",
           get16(p + 4), get16(p + 6), file->size);
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        printf("%-14s 0x%08x\n", names[i], get32(p + 8 + i * 4));
    for (uint32_t i = 0; i < reloc_count; ++i) {
        uint32_t rva = get32(p + reloc_off + (size_t)i * 4);
        if (image_size < 4 || rva > image_size - 4) {
            fprintf(stderr, "mkexe: relocation %u outside image\n", i);
            return -1;
        }
        printf("reloc[%u]      0x%08x\n", i, rva);
    }
    /* 仅结构检查; CRC、保留表及重定位语义由后续完整校验器处理。 */
    return 0;
}

int mkexe_from_elf(const MkexeBuffer *elf, const MkexeOptions *options,
                   MkexeImage *out) {
    (void)elf; (void)options; (void)out;
    fprintf(stderr, "mkexe: ELF -> LWP conversion is not implemented yet\n");
    return -1;
}

int mkexe_write_file(const char *path, const MkexeImage *image) {
    (void)path; (void)image;
    fprintf(stderr, "mkexe: LWP writer is not implemented yet\n");
    return -1;
}

void mkexe_image_free(MkexeImage *image) {
    free(image->image);
    free(image->relocs);
    memset(image, 0, sizeof(*image));
}

int main(int argc, char **argv) {
    MkexeBuffer input = {0};
    MkexeImage image = {0};
    int status;
    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        printf("Usage: mkexe input.elf output.lwp\n       mkexe -d file.lwp\n");
        return 0;
    }
    if (argc != 3 || argv[2][0] == '-' ||
        (argv[1][0] == '-' && strcmp(argv[1], "-d"))) {
        fprintf(stderr, "Usage: mkexe input.elf output.lwp | mkexe -d file.lwp\n");
        return 2;
    }
    int dump = !strcmp(argv[1], "-d");
    if (mkexe_read_file(argv[dump ? 2 : 1], &input)) return 1;
    if (dump) {
        status = mkexe_dump(&input);
    } else {
        MkexeOptions options = {argv[1], argv[2], 0, MKEXE_DEFAULT_STACK, 0};
        status = mkexe_from_elf(&input, &options, &image);
        if (!status) status = mkexe_write_file(options.output_path, &image);
    }
    mkexe_image_free(&image);
    mkexe_buffer_free(&input);
    return status ? 1 : 0;
}
