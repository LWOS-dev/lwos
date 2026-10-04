#ifndef _LW_ATA_H
#define _LW_ATA_H

#include "io.h"

#define ATA_PRIMARY_BASE      0x1F0
#define ATA_SECONDARY_BASE    0x170
#define ATA_PRIMARY_CTRL      0x3F6
#define ATA_SECONDARY_CTRL    0x376

#define ATA_REG_DATA          0
#define ATA_REG_ERROR         1
#define ATA_REG_FEATURES      1
#define ATA_REG_SECCOUNT0     2
#define ATA_REG_LBA0          3
#define ATA_REG_LBA1          4
#define ATA_REG_LBA2          5
#define ATA_REG_HDDEVSEL      6
#define ATA_REG_COMMAND       7
#define ATA_REG_STATUS        7

#define ATA_REG_SECCOUNT1     2
#define ATA_REG_LBA3          3
#define ATA_REG_LBA4          4
#define ATA_REG_LBA5          5

#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_IDENTIFY_PKT  0xA1      /* ATAPI answers this one */
#define ATA_CMD_READ_PIO      0x20
#define ATA_CMD_READ_PIO_EXT  0x24
#define ATA_CMD_WRITE_PIO     0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_FLUSH_CACHE   0xE7
#define ATA_CMD_FLUSH_CACHE_E 0xEA

#define ATA_SR_BSY            0x80
#define ATA_SR_DRDY           0x40
#define ATA_SR_DF             0x20
#define ATA_SR_DSC            0x10
#define ATA_SR_DRQ            0x08
#define ATA_SR_CORR           0x04
#define ATA_SR_IDX            0x02
#define ATA_SR_ERR            0x01

#define ATA_DEV_UNKNOWN       0
#define ATA_DEV_PATA          1
#define ATA_DEV_PATAPI        2

#define ATA_MAX_DEVICES       4

#define E_ARG      -1
#define E_TIMEOUT  -2
#define E_DEVERR   -3
#define E_FAULT    -4
#define E_NODRQ    -5
#define E_NODEV    -6
#define E_NOLBA48  -7
#define E_FLUSH    -8
#define E_ATAPI    -9

#define ATA_BACKEND_LEGACY     0   /* PIO on 0x1F0/0x170, this file */
#define ATA_BACKEND_AHCI       1   /* MMIO command ring, ahci.c */

typedef struct _ATA_DEV {
	BYTE    present;
	BYTE    type;
    // 0pri 1sec | AHCI:port idx
	BYTE    channel;  
    // 0master 1slave | AHCI:unused===0
	BYTE    drive; 
    // lba48 supported
	BYTE    lba48;   
	WORD    signature;
	QWORD   sectors;
	char    model[41];
	char    serial[21];
	char    firmware[9];
	BYTE    backend;
} ATA_DEV, *PATA_DEV;
typedef const ATA_DEV *PCATA_DEV;

void ata_init(void);
/* 更新设备表, 不输出文字; quiet 为兼容旧调用的别名。
 * 仅在启动/重新探测阶段调用, 不得与读写或已注册块设备并发。
 */
void ata_detect(void);
void ata_detect_quiet(void);
BYTE ata_count(void);
PCATA_DEV ata_get(BYTE index);

int ata_read(
    BYTE index, 
    QWORD lba,
    DWORD sectors,
    PVOID buf
);
int ata_write(
    BYTE index,
    QWORD lba,
    DWORD sectors,
    PCVOID buf
);

const char *ata_strerror(int err);

void ata_identify_parse(
    PATA_DEV d,
    const WORD id[256]
);

/* 将探测结果注册到块设备表; 注册表满时返回 -1。 */
int ata_register_blockdevs(void);

#endif