#include "dev/ata.h"
#include "text.h"
#include "dev/blockdev.h"

static ATA_DEV devs[ATA_MAX_DEVICES];
static BYTE n_devs;

const char *ata_strerror(int err) {
    switch (err) {
        case 0:         return "OK\0";
        case E_ARG:     return "BAD ARG\0";
        case E_TIMEOUT: return "TIMEOUT\0";
        case E_DEVERR:  return "DRV ERR\0";
        case E_FAULT:   return "DRV FAULT\0";
        case E_NODRQ:   return "DRV NEVER ASSERTED DRQ\0";
        case E_NODEV:   return "NO DEVICE\0";
        case E_NOLBA48: return "LBA BEYOND 28\0";
        case E_FLUSH:   return "FAILED FLUSH\0";
        case E_ATAPI:   return "ATAPI DEV\0";
    }
    return "UNK ERR\0";
}

static DWORD base_of(BYTE channel) {return channel?ATA_SECONDARY_BASE:ATA_PRIMARY_BASE;}
static DWORD ctrl_of(BYTE channel) {return channel?ATA_SECONDARY_CTRL:ATA_PRIMARY_CTRL;}

static void ata_delay400(BYTE channel) {
    WORD c = ctrl_of(channel);

    for (int i = 0; i < 4; i++) {
        (void)inb(c);
    }
}

static int ata_wait(BYTE channel, int data) {
    WORD b = base_of(channel);
    BYTE st = 0;

    for (int i = 0; i < 1000000; i++) {
        st = inb(b+ATA_REG_STATUS);
        if (st==0xff) {
            return E_NODEV;
        }
        if (!(st&ATA_SR_BSY)) {
            break;
        }
    }

    if (st&ATA_SR_BSY) {
        return E_TIMEOUT;
    }
    if (!data) {
        return 0;
    }

    if (st&ATA_SR_ERR) {
        return E_DEVERR;
    }
    if (st&ATA_SR_DF) {
        return E_FAULT;
    }

    for (int i = 0; i < 1000000; i++) {
        st = inb(b+ATA_REG_STATUS);
        if (st&(ATA_SR_ERR|ATA_SR_DF)) {
            return (st&ATA_SR_ERR)?E_DEVERR:E_FAULT;
        }
        if (st&ATA_SR_DRQ) {
            return 0;
        }
    }
    return E_NODRQ;
}

static void ata_select(BYTE channel, BYTE drive) {
    outb(base_of(channel)+ATA_REG_HDDEVSEL, (BYTE)(0xa0|(drive<<4)));
    ata_delay400(channel);
}
static void copy_string(char* dst, PCWORD src, int words) {
    int n = words*2,i;

    for (i=0; i<words; i++) {
        dst[i*2]    = (BYTE)(src[i]>>8);
        dst[i*2+1]  = (BYTE)(src[i]&0xff);
    }
    while (n>0&&(dst[n-1]==' '||dst[n-1]==0)) {
        n--;
    }
    dst[n]=0;
}

void ata_identify_parse(PATA_DEV d, const WORD id[256]) {
	d->signature = id[0];
	d->lba48     = (id[83] & 0x400) != 0;

	copy_string(d->model,    &id[27], 20);
	copy_string(d->serial,   &id[10], 10);
	copy_string(d->firmware, &id[23], 4);

	if (d->lba48)
		d->sectors = ((QWORD)id[103] << 48) | ((QWORD)id[102] << 32) |
		             ((QWORD)id[101] << 16) | (QWORD)id[100];
	else
		d->sectors = ((DWORD)id[61] << 16) | (DWORD)id[60];

	/* ATAPI reports capacity through READ CAPACITY, not IDENTIFY */
	if (d->type == ATA_DEV_PATAPI)
		d->sectors = 0;
}

static int ata_identify(BYTE channel, BYTE drive, PATA_DEV d) {
	WORD b = base_of(channel);
	WORD id[256];
	BYTE st, cl, ch;
	BYTE cmd = ATA_CMD_IDENTIFY;
	int r;

	ata_select(channel, drive);

	/* Clear these before IDENTIFY: a stale non-zero LBA makes some drives
	 * answer as if addressed, and the signature read below needs them. */
	outb(b + ATA_REG_SECCOUNT0, 0);
	outb(b + ATA_REG_LBA0, 0);
	outb(b + ATA_REG_LBA1, 0);
	outb(b + ATA_REG_LBA2, 0);

	outb(b + ATA_REG_COMMAND, cmd);
	ata_delay400(channel);

	st = inb(b + ATA_REG_STATUS);
	if (st == 0 || st == 0xFF)
		return E_NODEV;

	r = ata_wait(channel, 0);
	if (r)
		return r;

	/* An ATAPI device aborts IDENTIFY and leaves its signature in the LBA
	 * mid/high registers. Re-ask with the packet command instead of reading
	 * garbage, which is what the original driver did. */
	cl = inb(b + ATA_REG_LBA1);
	ch = inb(b + ATA_REG_LBA2);
	if ((cl == 0x14 && ch == 0xEB) || (cl == 0x69 && ch == 0x96)) {
		d->type = ATA_DEV_PATAPI;
		cmd = ATA_CMD_IDENTIFY_PKT;
		outb(b + ATA_REG_COMMAND, cmd);
		ata_delay400(channel);
	} else {
		d->type = ATA_DEV_PATA;
	}

	r = ata_wait(channel, 1);
	if (r)
		return r;

	for (int i = 0; i < 256; i++)
		id[i] = inw(b + ATA_REG_DATA);

	d->present   = 1;
	d->channel   = channel;
	d->drive     = drive;
	d->backend   = ATA_BACKEND_LEGACY;

	ata_identify_parse(d, id);

	return 0;
}

void ata_init(void) {
	for (DWORD i = 0; i < sizeof(devs); i++)
		((PBYTE)devs)[i] = 0;
	n_devs = 0;

	// nIEN: poll, so keep the drives from raising IRQ14/15
	outb(ATA_PRIMARY_CTRL, 0x02);
	outb(ATA_SECONDARY_CTRL, 0x02);
}

BYTE ata_count(void) { return n_devs; }

PCATA_DEV ata_get(BYTE index) {
    return index<n_devs?&devs[index]:0;
}

static void print_dev_tail(PCATA_DEV d) {
	puts("\n\rDISX: ");
    puts(d->type==ATA_DEV_PATAPI?"ATAPI":"ATA");
    puts(d->lba48?" LBA48 ":" LBA28 ");
    put_dword((DWORD)d->sectors);
    puts(" SECTORS (");
    put_dword((DWORD)(d->sectors>>11));
    puts("M)\n\r      SERIAL ");
    puts((char*)d->serial);
}

void ata_detect(void) {
	static const char *const chan_name[] = { "PRI", "SEC" };
	static const char *const drv_name[]  = { "MASTER", "SLAVE " };

	ata_init();

	puts("ATA PROBE, PIO\n\r");

	for (BYTE channel = 0; channel < 2; channel++) {
		WORD b = base_of(channel);

		if (inb(b + ATA_REG_STATUS) == 0xFF) {
			puts((char*)chan_name[channel]);
			puts(" CH=FFH, NO CTRL\n\r");
			continue;
		}

		for (BYTE drive = 0; drive < 2; drive++) {
			PATA_DEV d = &devs[n_devs];
			int r;

			if (n_devs >= ATA_MAX_DEVICES)
				break;

			/* Scratch test: write a pattern to two registers the
			 * drive must hold, and see if it comes back. */
			ata_select(channel, drive);
			outb(b + ATA_REG_SECCOUNT0, 0x55);
			outb(b + ATA_REG_LBA0, 0xAA);
			if (inb(b + ATA_REG_SECCOUNT0) != 0x55 ||
			    inb(b + ATA_REG_LBA0) != 0xAA)
				continue;

			r = ata_identify(channel, drive, d);
			if (r) {
				if (r != E_NODEV) {
					puts((char*)chan_name[channel]);
					putc(' ');
					puts((char*)drv_name[drive]);
					puts(": IDENTIFY FAILED, ");
					puts((char*)ata_strerror(r));
					puts("\n\r");
				}
				d->present = 0;
				continue;
			}

			puts("dev");
			put_byte(n_devs);
			puts("  ");
			puts((char*)chan_name[channel]);
			putc(' ');
			puts((char*)drv_name[drive]);
			puts("  ");
			puts(d->model);
			print_dev_tail(d);
			n_devs++;
		}
	}

    puts("DEV FOUND: ");
	put_byte(n_devs);
	puts("\n\r");
}

void ata_detect_quiet(void) {
	ata_init();

	for (BYTE channel = 0; channel < 2; channel++) {
		WORD b = base_of(channel);

		if (inb(b + ATA_REG_STATUS) == 0xFF) {
			continue;
		}

		for (BYTE drive = 0; drive < 2; drive++) {
			PATA_DEV d = &devs[n_devs];
			int r;

			if (n_devs >= ATA_MAX_DEVICES)
				break;

			/* Scratch test: write a pattern to two registers the
			 * drive must hold, and see if it comes back. */
			ata_select(channel, drive);
			outb(b + ATA_REG_SECCOUNT0, 0x55);
			outb(b + ATA_REG_LBA0, 0xAA);
			if (inb(b + ATA_REG_SECCOUNT0) != 0x55 ||
			    inb(b + ATA_REG_LBA0) != 0xAA)
				continue;

			r = ata_identify(channel, drive, d);
			if (r) {
				d->present = 0;
				continue;
			}
			n_devs++;
		}
	}
}

static int setup_lba28(PCATA_DEV d, DWORD lba, BYTE sectors) {
    WORD b = base_of(d->channel);

    outb(b+ATA_REG_HDDEVSEL,
        (BYTE)(0xe0|(d->drive<<4)|((lba>>24)&0x0f)));
    ata_delay400(d->channel);

    outb(b+ATA_REG_SECCOUNT0, sectors);
    outb(b+ATA_REG_LBA0, (BYTE)(lba));
    outb(b+ATA_REG_LBA1, (BYTE)(lba>>8));
    outb(b+ATA_REG_LBA2, (BYTE)(lba>>16));
    return ata_wait(d->channel, 0);
}

static int setup_lba48(PCATA_DEV d, QWORD lba, WORD sectors) {
    WORD b = base_of(d->channel);

    outb(b+ATA_REG_HDDEVSEL,
        (BYTE)(0x40|(d->drive<<4)));
    ata_delay400(d->channel);

    outb(b+ATA_REG_SECCOUNT1, (BYTE)(sectors>>8));
    outb(b+ATA_REG_LBA3, (BYTE)(lba>>24));
    outb(b+ATA_REG_LBA4, (BYTE)(lba>>32));
    outb(b+ATA_REG_LBA5, (BYTE)(lba>>40));

    outb(b+ATA_REG_SECCOUNT0, (BYTE)sectors);
    outb(b+ATA_REG_LBA0, (BYTE)(lba));
    outb(b+ATA_REG_LBA1, (BYTE)(lba>>8));
    outb(b+ATA_REG_LBA2, (BYTE)(lba>>16));
    return ata_wait(d->channel, 0);
}

static int xfer(BYTE index, 
                QWORD lba, 
                DWORD sectors, 
                PVOID buf,
                int write) {
    PCATA_DEV d = ata_get(index);
    WORD b;
    int use48, r;

    if (!d || !d->present) {
        return E_NODEV;
    }
    if (!sectors || !buf) {
        return E_ARG;
    }
    if (d->type == ATA_DEV_PATAPI) {
        return E_ATAPI;
    }

    if (d->backend == ATA_BACKEND_AHCI) {
        return E_ATAPI;
    }
    b = base_of(d->channel);
    use48 = (lba+sectors>0x10000000u)||sectors>256;

    if (use48 && !d->lba48) {
        return E_NOLBA48;
    }
    if (use48?(sectors>65536):(sectors>256)) {
        return E_ARG;
    }

    if (use48) {
        r=setup_lba48(d, lba, (WORD)(sectors==65536?0:sectors));
        if (r)return r;
        outb(b+ATA_REG_COMMAND,
            write?ATA_CMD_WRITE_PIO_EXT:ATA_CMD_READ_PIO_EXT);
    } else {
        r=setup_lba28(d, (DWORD)lba, (BYTE)(sectors==256?0:sectors));
        if (r)return r;
        outb(b+ATA_REG_COMMAND,
            write?ATA_CMD_WRITE_PIO:ATA_CMD_READ_PIO);
    }

    PWORD p = (PWORD)buf;

    for (DWORD s = 0; s < sectors; s++) {
        r=ata_wait(d->channel, 1);
        if (r) {
            return r;
        }

        if (write) {
            for (int i = 0; i < 256; i++) {
                outw(b+ATA_REG_DATA, p[i]);
            }
            r=ata_wait(d->channel, 0);
            if (r)return r;
        } else {
            for (int i = 0; i < 256; i++) {
                p[i] = inw(b+ATA_REG_DATA);
            }
        }
        p+=256;
    }

    if (write) {
        outb(b+ATA_REG_COMMAND,
            d->lba48?ATA_CMD_FLUSH_CACHE_E:ATA_CMD_FLUSH_CACHE);
        if (ata_wait(d->channel, 0)) {
            return E_FLUSH;
        }
    }
    return 0;
}
int ata_read(
    BYTE index,
    QWORD lba,
    DWORD sectors,
    PVOID buf
) {
    return xfer(index, lba, sectors, buf, 0);
}
int ata_write(
    BYTE index,
    QWORD lba,
    DWORD sectors,
    PCVOID buf
) {
    return xfer(index, lba, sectors, (PVOID)buf, 1);
}

/* PIO 适配层: 对象与上下文驻留在 ABI.BIN 中。 */
typedef struct {
    BYTE index;
} PIO_BLK_CTX;

static BLKDEV block_devices[ATA_MAX_DEVICES];
static PIO_BLK_CTX block_contexts[ATA_MAX_DEVICES];

static int pio_blk_xfer(PBLKDEV d, QWORD lba, DWORD count,
                        PVOID buf, int write) {
    if (!d || !d->priv || !buf || !count || lba >= d->sectors ||
        (QWORD)count > d->sectors - lba)
        return E_ARG;
    PIO_BLK_CTX *ctx = d->priv;
    PCATA_DEV a = ata_get(ctx->index);
    if (!a)
        return E_NODEV;
    PBYTE p = buf;
    while (count) {
        /* 保守地每批最多 256 扇区, 底层自行选择 LBA28 / LBA48。 */
        DWORD batch = count > 256 ? 256 : count;
        int r = write ? ata_write(ctx->index, lba, batch, p)
                      : ata_read(ctx->index, lba, batch, p);
        if (r)
            return r;
        p += batch * 512u;
        lba += batch;
        count -= batch;
    }
    return 0;
}

static int pio_blk_read(PBLKDEV d, QWORD lba, DWORD count, PVOID buf) {
    return pio_blk_xfer(d, lba, count, buf, 0);
}

static int pio_blk_write(PBLKDEV d, QWORD lba, DWORD count, PCVOID buf) {
    return pio_blk_xfer(d, lba, count, (PVOID)buf, 1);
}

static int pio_blk_flush(PBLKDEV d) {
    if (!d || !d->priv)
        return E_ARG;
    PIO_BLK_CTX *ctx = d->priv;
    PCATA_DEV a = ata_get(ctx->index);
    if (!a || !a->present)
        return E_NODEV;
    ata_select(a->channel, a->drive);
    int r = ata_wait(a->channel, 0);
    if (r)
        return r;
    outb(base_of(a->channel) + ATA_REG_COMMAND,
         a->lba48 ? ATA_CMD_FLUSH_CACHE_E : ATA_CMD_FLUSH_CACHE);
    ata_delay400(a->channel);
    r = ata_wait(a->channel, 0);
    if (r)
        return r;
    BYTE st = inb(base_of(a->channel) + ATA_REG_STATUS);
    return st & (ATA_SR_ERR | ATA_SR_DF) ? E_FLUSH : 0;
}

int ata_register_blockdevs(void) {
    for (BYTE i = 0; i < ata_count(); i++) {
        PCATA_DEV a = ata_get(i);
        if (!a->present || a->type != ATA_DEV_PATA || !a->sectors)
            continue;
        PBLKDEV d = &block_devices[i];
        *d = (BLKDEV){
            .version = BLKDEV_VERSION, .size = sizeof(*d),
            .name = "ata0", .sector_size = 512, .sectors = a->sectors,
            .read = pio_blk_read, .write = pio_blk_write,
            .flush = pio_blk_flush, .poll = 0,
            .priv = &block_contexts[i]
        };
        d->name[3] = '0' + i;
        block_contexts[i].index = i;
        if (blkdev_register(d) < 0)
            return -1;
    }
    return 0;
}
