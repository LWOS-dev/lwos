# Preloaded by BOOT.INI; called by monitor, using monitor's stack.
RM_DIR := app/resman
RM_ELF := $(BIN_DIR)/resman.elf
RM_BIN := $(BIN_DIR)/resman.bin
RM_OBJS := $(BUILD_DIR)/$(RM_DIR)/head.o $(BUILD_DIR)/$(RM_DIR)/resman.o
# resman 显式选择自己的库，使用 lib/build.mk 的 KERN 编译规则。
# gfx 依赖 mem 的 memcpy，mem 的分配器依赖 bitmap。
RM_LIB_SRCS := mem.c \
			   bitmap.c \
			   gfx.c \
			   ui.c \
			   task.c \
			   mouse.c
RM_LIB_OBJS := $(RM_LIB_SRCS:%.c=$(BUILD_DIR)/lib/kern/%.o)

$(BUILD_DIR)/$(RM_DIR)/%.o: $(RM_DIR)/src/%.c
	@mkdir -p $(@D)
	$(TOOL_C) $(CFLAGS_KERN) $(LIB_INC) -I$(RM_DIR)/include -c $< -o $@

$(RM_ELF): $(RM_OBJS) $(RM_LIB_OBJS) $(RM_DIR)/resman.ld
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) -T $(RM_DIR)/resman.ld -o $@ \
		$(RM_OBJS) $(RM_LIB_OBJS)

FSROOT_FILES += $(FSROOT)/RESMAN.BIN
$(FSROOT)/RESMAN.BIN: $(RM_BIN)
