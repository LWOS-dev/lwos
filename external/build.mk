# 外部程序: 每个 .c 对应一个可重定位 LWP EXE, 只通过公开 ABI 调用系统。
EXTERNAL_NAMES = textdemo memtest memloop a b exit ghost reddot \
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
$(BUILD_DIR)/external/a.o $(BUILD_DIR)/external/b.o: external/fpu_check.h
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

# GUI host 单独链接图形/UI 工具库；其他外部程序仍使用单对象链接规则。
GHOST_LIB_OBJS := $(addprefix $(BUILD_DIR)/lib/app/, \
    gfx.o gui.o ui.o mem.o bitmap.o convert.o string.o fs.o app.o)

$(BIN_DIR)/external/ghost.elf: $(BUILD_DIR)/external/ghost.o $(GHOST_LIB_OBJS) tools/mkexe/lwp.ld external/build.mk
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) --emit-relocs -T tools/mkexe/lwp.ld \
	    $(filter %.o,$^) $(LIBGCC) -o $@

$(BIN_DIR)/external/%.elf: $(BUILD_DIR)/external/%.o tools/mkexe/lwp.ld
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) --emit-relocs -T tools/mkexe/lwp.ld $< $(LIBGCC) -o $@

$(BIN_DIR)/external/%.exe: $(BIN_DIR)/external/%.elf $(EXTERNAL_MKEXE)
	$(EXTERNAL_MKEXE) $< $@

FSROOT_FILES += $(FSROOT)/GFX/REDDOT.EXE
FSROOT_FILES += $(FSROOT)/GFX/SHADER.EXE
FSROOT_FILES += $(FSROOT)/GFX/XTAL.EXE
FSROOT_FILES += $(FSROOT)/APP/CALC.EXE
FSROOT_FILES += $(FSROOT)/A.EXE
FSROOT_FILES += $(FSROOT)/B.EXE
FSROOT_FILES += $(FSROOT)/GHOST.EXE

$(FSROOT)/GFX/XTAL.EXE: $(BIN_DIR)/external/xtal.exe
$(FSROOT)/GFX/SHADER.EXE: $(BIN_DIR)/external/shader.exe
$(FSROOT)/GFX/REDDOT.EXE: $(BIN_DIR)/external/reddot.exe
$(FSROOT)/APP/CALC.EXE: $(BIN_DIR)/external/calc.exe
$(FSROOT)/A.EXE: $(BIN_DIR)/external/a.exe
$(FSROOT)/B.EXE: $(BIN_DIR)/external/b.exe
$(FSROOT)/GHOST.EXE: $(BIN_DIR)/external/ghost.exe

external: $(EXTERNAL_EXES)
.PHONY: external
.SECONDARY: $(addprefix $(BUILD_DIR)/external/,$(addsuffix .o,$(EXTERNAL_NAMES))) \
    $(addprefix $(BIN_DIR)/external/,$(addsuffix .elf,$(EXTERNAL_NAMES)))

FSROOT_FILES += $(FSROOT)/EXIT.EXE
$(FSROOT)/EXIT.EXE: $(BIN_DIR)/external/exit.exe

$(BUILD_DIR)/external/memtest.o $(BUILD_DIR)/external/memloop.o: external/memcheck.h
FSROOT_FILES += $(FSROOT)/MEMTEST.EXE $(FSROOT)/MEMLOOP.EXE
$(FSROOT)/MEMTEST.EXE: $(BIN_DIR)/external/memtest.exe
$(FSROOT)/MEMLOOP.EXE: $(BIN_DIR)/external/memloop.exe

$(BIN_DIR)/external/textdemo.exe: $(BIN_DIR)/external/textdemo.elf $(EXTERNAL_MKEXE)
	$(EXTERNAL_MKEXE) --text $< $@

FSROOT_FILES += $(FSROOT)/TEXTDEMO.EXE
$(FSROOT)/TEXTDEMO.EXE: $(BIN_DIR)/external/textdemo.exe
