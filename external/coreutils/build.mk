# Standalone text commands. Each source defines main(argc, argv).
COREUTIL_NAMES := $(basename $(notdir $(wildcard external/coreutils/*.c)))
COREUTIL_ELFS := $(addprefix $(BIN_DIR)/coreutils/,$(addsuffix .elf,$(COREUTIL_NAMES)))
COREUTIL_EXES := $(COREUTIL_ELFS:.elf=.exe)
COREUTIL_LIB_OBJS := $(addprefix $(BUILD_DIR)/lib/app/,string.o fs.o mem.o bitmap.o convert.o)
SED_PORT_OBJS := $(addprefix $(BUILD_DIR)/external/coreutils/,port/compat.o vendor/minised/sedcomp.o vendor/minised/sedexec.o)
ED_PORT_OBJS := $(addprefix $(BUILD_DIR)/external/coreutils/,port/compat.o vendor/busybox/ed.o)
$(BIN_DIR)/coreutils/sed.elf: $(SED_PORT_OBJS)
$(BIN_DIR)/coreutils/ed.elf: $(ED_PORT_OBJS)

$(BUILD_DIR)/external/coreutils/%.o: external/coreutils/%.c external/coreutils/build.mk
	@mkdir -p $(@D)
	$(TOOL_C) $(EXTERNAL_CFLAGS) $(LIB_INC) -c $< -o $@

$(COREUTIL_ELFS): $(BIN_DIR)/coreutils/%.elf: $(BUILD_DIR)/external/coreutils/%.o \
    $(CRT_OBJS) $(STDIO_LIB_OBJS) $(COREUTIL_LIB_OBJS) tools/mkexe/lwp.ld external/coreutils/build.mk
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) --emit-relocs -T tools/mkexe/lwp.ld $(filter %.o,$^) $(LIBGCC) -o $@

$(COREUTIL_EXES): $(BIN_DIR)/coreutils/%.exe: $(BIN_DIR)/coreutils/%.elf $(EXTERNAL_MKEXE)
	$(EXTERNAL_MKEXE) --text $< $@

# FAT 8.3 install names; adding cat.c/grep.c/sed.c needs no shell changes.
define COREUTIL_INSTALL
FSROOT_FILES += $(FSROOT)/BIN/$(shell printf '%s' $(1) | tr '[:lower:]' '[:upper:]').EXE
$(FSROOT)/BIN/$(shell printf '%s' $(1) | tr '[:lower:]' '[:upper:]').EXE: $(BIN_DIR)/coreutils/$(1).exe
endef
$(foreach name,$(COREUTIL_NAMES),$(eval $(call COREUTIL_INSTALL,$(name))))

FSROOT_FILES += $(FSROOT)/RES/LICENSE/SED.TXT $(FSROOT)/RES/LICENSE/ED.TXT
$(FSROOT)/RES/LICENSE/SED.TXT: external/coreutils/vendor/minised/LICENSE
$(FSROOT)/RES/LICENSE/ED.TXT: external/coreutils/vendor/busybox/LICENSE.ed

coreutils: $(COREUTIL_EXES)
external: coreutils
.PHONY: coreutils

$(BUILD_DIR)/tools/lwcon: tools/lwcon/lwcon.c lib/include/lwft.h
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -O2 -Wall -Wextra -Werror $< -o $@
lwcon: $(BUILD_DIR)/tools/lwcon

$(BUILD_DIR)/tests/lwcon_test: tests/lwcon_test.c tools/lwcon/lwcon.c lib/include/lwft.h
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -O2 -Wall -Wextra -Werror $< -o $@
test-lwcon: $(BUILD_DIR)/tests/lwcon_test
	$<

$(BUILD_DIR)/tests/net_init_test: tests/net_init_test.c app/resman/src/net.c \
    $(wildcard lib/include/*.h) $(wildcard app/resman/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
	    -Wno-int-to-pointer-cast -fno-builtin $(LIB_INC) $< -o $@
test-net-init: $(BUILD_DIR)/tests/net_init_test
	$<

test-netrecv: $(BIN_DIR)/abi.bin $(BIN_DIR)/resman.bin $(BIN_DIR)/monitor.bin $(BIN_DIR)/coreutils/netrecv.exe \
    $(BIN_DIR)/external/ccp.exe \
    $(BUILD_DIR)/lib/app/app.o $(BUILD_DIR)/tools/lwcon
	PYTHONDONTWRITEBYTECODE=1 python3 tests/netrecv_test.py
.PHONY: lwcon test-lwcon test-net-init test-netrecv
.SECONDARY: $(COREUTIL_ELFS) $(addprefix $(BUILD_DIR)/external/coreutils/,$(addsuffix .o,$(COREUTIL_NAMES)))

$(BUILD_DIR)/tests/ls_test: tests/ls_test.c external/coreutils/ls.c lib/src/string.c $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
	    -Wno-return-type -fno-builtin -ffunction-sections -fdata-sections $(LIB_INC) \
	    tests/ls_test.c lib/src/string.c -Wl,--gc-sections -o $@

test-ls: $(BUILD_DIR)/tests/ls_test
	$<
.PHONY: test-ls

$(BUILD_DIR)/tests/cat_test: tests/cat_test.c external/coreutils/cat.c $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
	    -fno-builtin $(LIB_INC) $< -o $@

test-cat: $(BUILD_DIR)/tests/cat_test
	$<
.PHONY: test-cat

$(BUILD_DIR)/tests/xxd_test: tests/xxd_test.c external/coreutils/xxd.c $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
	    -fno-builtin $(LIB_INC) $< -o $@

test-xxd: $(BUILD_DIR)/tests/xxd_test
	$<
.PHONY: test-xxd

$(BUILD_DIR)/tests/touch_test: tests/touch_test.c external/coreutils/touch.c $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
	    -fno-builtin $(LIB_INC) $< -o $@

test-touch: $(BUILD_DIR)/tests/touch_test
	$<
.PHONY: test-touch

$(BUILD_DIR)/tests/rm_test: tests/rm_test.c external/coreutils/rm.c $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast \
	    -fno-builtin $(LIB_INC) $< -o $@

test-rm: $(BUILD_DIR)/tests/rm_test test-fs-dir
	$<
.PHONY: test-rm
