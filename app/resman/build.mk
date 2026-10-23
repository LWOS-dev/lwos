# Preloaded by BOOT.INI; called by monitor, using monitor's stack.
RM_DIR := app/resman
RM_ELF := $(BIN_DIR)/resman.elf
RM_BIN := $(BIN_DIR)/resman.bin
RM_OBJS := $(BUILD_DIR)/$(RM_DIR)/head.o \
           $(BUILD_DIR)/$(RM_DIR)/resman.o \
           $(BUILD_DIR)/$(RM_DIR)/autoload.o \
           $(BUILD_DIR)/$(RM_DIR)/task.o \
           $(BUILD_DIR)/$(RM_DIR)/alloc.o \
           $(BUILD_DIR)/$(RM_DIR)/task_start.o \
		   $(BUILD_DIR)/$(RM_DIR)/image.o \
           $(BUILD_DIR)/$(RM_DIR)/imgcall.o \
           $(BUILD_DIR)/$(RM_DIR)/tui.o \
           $(BUILD_DIR)/$(RM_DIR)/isr.o \
           $(BUILD_DIR)/$(RM_DIR)/interrupt.o \
           $(BUILD_DIR)/$(RM_DIR)/keyboard.o \
           $(BUILD_DIR)/$(RM_DIR)/net.o \
		   $(BUILD_DIR)/$(RM_DIR)/pit.o \
           $(BUILD_DIR)/$(RM_DIR)/double_fault.o
# resman 显式选择自己的库，使用 lib/build.mk 的 KERN 编译规则。
# 初始化内存页分配器和 GDT；鼠标由 resman 接管，图形、UI 由应用按需链接。
RM_LIB_SRCS := mouse.c mem.c \
               fs.c \
               bitmap.c \
               task.c \
			   convert.c \
			   string.c
RM_LIB_OBJS := $(RM_LIB_SRCS:%.c=$(BUILD_DIR)/lib/kern/%.o)

$(BUILD_DIR)/$(RM_DIR)/%.o: $(RM_DIR)/src/%.c
	@mkdir -p $(@D)
	$(TOOL_C) $(CFLAGS_KERN) $(LIB_INC) -I$(RM_DIR)/include -c $< -o $@

$(BUILD_DIR)/$(RM_DIR)/%.o: $(RM_DIR)/src/%.s
	@mkdir -p $(@D)
	$(TOOL_ASM) $(ASFLAGS) $< -o $@

$(RM_ELF): $(RM_OBJS) $(RM_LIB_OBJS) $(RM_DIR)/resman.ld
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) -T $(RM_DIR)/resman.ld -o $@ \
		$(RM_OBJS) $(RM_LIB_OBJS)

FSROOT_FILES += $(FSROOT)/RESMAN.BIN
$(FSROOT)/RESMAN.BIN: $(RM_BIN)

$(BUILD_DIR)/tests/window_event_test: tests/window_event_test.c $(RM_DIR)/src/image.c \
    $(wildcard $(RM_DIR)/include/*.h) $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -fno-builtin -Wno-pointer-to-int-cast \
	    -ffunction-sections -fdata-sections $(LIB_INC) \
	    tests/window_event_test.c $(RM_DIR)/src/image.c -Wl,--gc-sections -o $@

test-window-event: $(BUILD_DIR)/tests/window_event_test
	$<
.PHONY: test-window-event

test-ghost-restart: $(IMG)
	python3 -B tests/ghost_restart_test.py
.PHONY: test-ghost-restart
