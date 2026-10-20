# 外部程序: 每个 .c 对应一个可重定位 LWP EXE, 只通过公开 ABI 调用系统。
EXTERNAL_NAMES = reddot \
				 shader \
				 xtal \
				 calc
EXTERNAL_EXES = $(addprefix $(BIN_DIR)/external/,$(addsuffix .exe,$(EXTERNAL_NAMES)))
EXTERNAL_CFLAGS = $(CFLAGS_APP) -nostdinc -fno-pic \
    -fno-asynchronous-unwind-tables -fno-unwind-tables -Werror
EXTERNAL_MKEXE = $(BUILD_DIR)/tools/mkexe

$(EXTERNAL_MKEXE): tools/mkexe/mkexe.c tools/mkexe/mkexe.h tools/mkexe/hdr.h \
    lib/include/lwp.h lib/include/stdint.h tools/mkexe/Makefile
	$(MAKE) -C tools/mkexe TARGET=$(abspath $@)

# 让旧示例与外部程序并行构建时共享同一次宿主工具构建。
$(MKEXE_TEST_EXE): $(EXTERNAL_MKEXE)

$(BUILD_DIR)/external/%.o: external/%.c external/build.mk
	@mkdir -p $(@D)
	$(TOOL_C) $(EXTERNAL_CFLAGS) $(LIB_INC) -c $< -o $@

# x87 keeps intermediates in extended precision otherwise; xtal requests float.
$(BUILD_DIR)/external/xtal.o: EXTERNAL_CFLAGS += -fexcess-precision=standard
$(BUILD_DIR)/external/xtal.o: external/xtal_lut.h

external/xtal_lut.h: tools/gen_xtal_lut.py
	python3 $<

# Native numerical and finite-image checks; no OS image/emulator required.
$(BUILD_DIR)/tests/xtal_test: tests/xtal_test.c external/xtal.c external/xtal_lut.h
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -O2 -Wall -Wextra -Werror $< -lm -o $@

test-xtal: $(BUILD_DIR)/tests/xtal_test
	$<
.PHONY: test-xtal

$(BIN_DIR)/external/%.elf: $(BUILD_DIR)/external/%.o tools/mkexe/lwp.ld
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) --emit-relocs -T tools/mkexe/lwp.ld $< $(LIBGCC) -o $@

$(BIN_DIR)/external/%.exe: $(BIN_DIR)/external/%.elf $(EXTERNAL_MKEXE)
	$(EXTERNAL_MKEXE) $< $@

FSROOT_FILES += $(FSROOT)/GFX/REDDOT.EXE
FSROOT_FILES += $(FSROOT)/GFX/SHADER.EXE
FSROOT_FILES += $(FSROOT)/GFX/XTAL.EXE
FSROOT_FILES += $(FSROOT)/APP/CALC.EXE

$(FSROOT)/GFX/XTAL.EXE: $(BIN_DIR)/external/xtal.exe
$(FSROOT)/GFX/SHADER.EXE: $(BIN_DIR)/external/shader.exe
$(FSROOT)/GFX/REDDOT.EXE: $(BIN_DIR)/external/reddot.exe
$(FSROOT)/APP/CALC.EXE: $(BIN_DIR)/external/calc.exe

external: $(EXTERNAL_EXES)
.PHONY: external
.SECONDARY: $(addprefix $(BUILD_DIR)/external/,$(addsuffix .o,$(EXTERNAL_NAMES))) \
    $(addprefix $(BIN_DIR)/external/,$(addsuffix .elf,$(EXTERNAL_NAMES)))
