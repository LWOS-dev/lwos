CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror
TARGET = ../../build/tools/mkexe

all: $(TARGET)

$(TARGET): mkexe.c mkexe.h ../../lib/include/lwp.h ../../lib/include/stdint.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) mkexe.c -o $@

.PHONY: all

# 裸机程序与宿主工具使用不同编译选项。
TEST_DIR = ../../build/tools/mkexe-test
TEST_CFLAGS = -m32 -std=gnu11 -Os -Wall -Wextra -Werror \
	-ffreestanding -nostdinc -nostdlib -fno-builtin -fno-pie -fno-pic \
	-fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-mno-80387 -mno-mmx -mno-sse -mno-sse2

test-elf: $(TEST_DIR)/test.elf

$(TEST_DIR)/test.o: test.c ../../lib/include/abi.h \
	../../lib/include/stdint.h ../../lib/include/dev/blockdev.h
	mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) -I../../lib/include -c $< -o $@

$(TEST_DIR)/test.elf: $(TEST_DIR)/test.o lwp.ld
	ld -m elf_i386 -nostdlib --emit-relocs --no-warn-rwx-segments \
		-T lwp.ld $< -o $@

.PHONY: test-elf

# AI 生成的学习参考单独构建, 不替代正式 mkexe。
REFERENCE = ../../build/tools/mkexe-reference
reference: $(REFERENCE)

$(REFERENCE): mkexe_reference.c mkexe.h ../../lib/include/lwp.h ../../lib/include/stdint.h
	mkdir -p $(@D)
	$(CC) $(CFLAGS) mkexe_reference.c -o $@

.PHONY: reference
