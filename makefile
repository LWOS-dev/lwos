.DEFAULT_GOAL := all

TOOL_ASM	= nasm
TOOL_C		= gcc
TOOL_LD		= ld

# 源码按运行时的四个世界分层, 每个世界自己的构建规则写在各自目录的 build.mk:
#   boot/    MBR + STAGE2, 实模式, 装进磁盘保留扇区
#   loader/  LOADER.BIN, 0x10000, 自带驱动, 不依赖 lib
#   lib/     库. lib/include 是对外公开的头 (abi.h 等), 谁都能用;
#            lib/src 是共享源码, 按 APP / KERN 显式选择并分别编译;
#            lib/abi 是 ABI.BIN (0x100000), 私有头文件只给它自己用
#   app/     跑在 ABI 之上的程序, 只能看见 lib/include
# 中间产物全部放 build/, 成品放 bin/, 源码目录里不留 .o

# ---------------------------------------------------------------- 编译选项分层
# KERN: loader / lib / 以后的 resman、sys. 禁止浮点: 这些代码会在任意 app
#       的上下文里运行, 碰了 FPU 就会弄坏 app 没保存的浮点状态.
#       写了 float 时 gcc 不会报错, 而是改调软浮点 (__adddf3 等);
#       KERN 层不链 libgcc, 所以会在链接时报 undefined reference.
# APP:  app. 允许 x87 (FPU 由 ABI 初始化), 链 libgcc 补 64 位除法和
#       long long <-> double 之类的转换.
ASFLAGS		= -f elf32
CFLAGS_BASE	= -m32 -ffreestanding -fno-pie \
			  -fno-stack-protector -fno-builtin -nostdlib \
			  -mno-mmx -mno-sse -mno-sse2 \
			  -Wall -Wextra -Os -std=gnu11 -MMD -MP
CFLAGS_KERN	= $(CFLAGS_BASE) -mno-80387 -mno-fp-ret-in-387
CFLAGS_APP	= $(CFLAGS_BASE) -mfpmath=387
LDFLAGS		= -m elf_i386 -nostdlib --no-warn-rwx-segments
LIBGCC		:= $(shell $(TOOL_C) -m32 -print-libgcc-file-name)

BIN_DIR		= bin
BUILD_DIR	= build

LIB_INC		= -Ilib/include

IMG			= lwcnc.img
IMG_SECTORS	= 131040
PART_LBA	= 2048

FSROOT		= fsroot
MKFS_FAT	= mkfs.fat
MCOPY		= mcopy
FAT_IMG		= $(BUILD_DIR)/fat32.img

DEVICE		= /dev/sda

# 各世界往这里追加自己要放进 FAT 分区的文件, 再声明它从哪来
FSROOT_FILES =

all: $(IMG) compile_commands.json

# ---------------------------------------------------------------- 各世界
# 必须在镜像规则之前 include: 依赖列表里的 $(FSROOT_FILES) 是立即展开的
include boot/build.mk
include loader/build.mk
include lib/build.mk
include lib/abi/build.mk
include app/monitor/build.mk
include app/test1/build.mk

# resources/ 的文件在镜像中放到 /RES/, 源文件不会被 clean 删除。
RESOURCE_SRCS := $(shell find resources -type f 2>/dev/null)
RESOURCE_FILES := $(patsubst resources/%,$(FSROOT)/RES/%,$(RESOURCE_SRCS))
FSROOT_FILES += $(RESOURCE_FILES)
ifneq ($(strip $(RESOURCE_FILES)),)
$(RESOURCE_FILES): $(FSROOT)/RES/%: resources/%
endif

# ---------------------------------------------------------------- 通用
# 逐文件编译参数供 VS Code C/C++ 和 clangd 使用, 区分裸机与宿主工具。
EDITOR_SOURCES := $(shell find lib app loader tools tests -name '*.c' 2>/dev/null)
EDITOR_MAKEFILES := $(wildcard boot/build.mk loader/build.mk lib/build.mk lib/*/build.mk app/*/build.mk)
compile_commands.json: makefile $(EDITOR_MAKEFILES) $(EDITOR_SOURCES) tools/gen_compile_commands.py
	python3 tools/gen_compile_commands.py

compdb:
	python3 tools/gen_compile_commands.py

$(BIN_DIR)/%.bin: $(BIN_DIR)/%.elf
	objcopy -O binary $< $@

# ---------------------------------------------------------------- fsroot / 镜像
$(FSROOT_FILES):
	@mkdir -p $(@D)
	cp $< $@

fsroot: $(FSROOT_FILES)

# 已有的手工文件和目录也触发镜像更新 (包括删除子文件后的目录时间变化)。
FSROOT_CONTENTS := $(shell find $(FSROOT) -mindepth 1 2>/dev/null)

$(IMG): $(MBR) $(STAGE2) $(FSROOT_FILES) $(FSROOT_CONTENTS) makefile
	@mkdir -p $(BUILD_DIR)
	truncate -s $$(( ($(IMG_SECTORS) - $(PART_LBA)) * 512 )) $(FAT_IMG)
	$(MKFS_FAT) -F 32 -S 512 -s 1 -R 32 -f 2 -h $(PART_LBA) -n "LWCNC FAT32" $(FAT_IMG)
	$(MCOPY) -s -i $(FAT_IMG) $(FSROOT)/* ::/
	rm -f $@
	truncate -s $$(( $(IMG_SECTORS) * 512)) $@
	dd if=$(MBR)	of=$@ bs=512 count=1 seek=0 conv=notrunc status=none
	dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(FAT_IMG) of=$@ bs=512 seek=$(PART_LBA) conv=notrunc status=none

resetimg:
	rm -f $(IMG)
	$(MAKE) $(IMG)

run: $(IMG)
	bochs -f bochsrc -q

# ---------------------------------------------------------------- 在线试玩
# 镜像不进仓库: CI 每次 push 到 main 会 make webimg 然后把产物覆盖挂到
# 固定 tag 的 GitHub Release 上 (滚动更新), demo 页从那里匿名拉取.
# 这个目标只用于本地调试 site/ 的 demo 页, 输出目录在 .gitignore 里.
DEMO_IMG_GZ	= site/public/assets/demo/lwcnc.img.gz

$(DEMO_IMG_GZ): $(IMG)
	@mkdir -p $(@D)
	gzip -9 -c $(IMG) > $@

webimg: $(DEMO_IMG_GZ)

# ---------------------------------------------------------------- 文件系统宿主测试
# 这里使用 Linux libc 做镜像读取, 不使用裸机的 -m32 编译选项。
FS_TEST = $(BUILD_DIR)/tests/fs_test
$(FS_TEST): tests/fs_test.c lib/src/fs.c lib/src/mem.c lib/src/string.c \
    lib/include/fs.h lib/include/mem.h lib/include/string.h \
    lib/include/stdint.h lib/include/dev/blockdev.h
	@mkdir -p $(@D)
	gcc -std=gnu11 -fno-builtin -Wall -Wextra -Ilib/include \
	    tests/fs_test.c lib/src/fs.c lib/src/mem.c lib/src/string.c -o $@

test-fs: $(IMG) $(FS_TEST)
	$(FS_TEST) $(IMG)

clean:
	rm -f $(IMG) $(DEMO_IMG_GZ)
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(FSROOT)

.PHONY: all resetimg run clean fsroot webimg test-fs compdb

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
