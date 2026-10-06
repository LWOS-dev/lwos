# 共享源码不决定运行时归属: APP 工具库与 KERN 硬件库分别编译。
# 显式列出源码, 避免新驱动自动进入每个应用。
APP_LIB_SRCS := string.c \
				convert.c \
				mem.c \
				fs.c \
				bitmap.c
APP_LIB_OBJS := $(APP_LIB_SRCS:%.c=$(BUILD_DIR)/lib/app/%.o)

KERN_LIB_SRCS := io.c pci.c dev/ata.c dev/blockdev.c
KERN_LIB_OBJS := $(KERN_LIB_SRCS:%.c=$(BUILD_DIR)/lib/kern/%.o)

$(BUILD_DIR)/lib/app/%.o: lib/src/%.c
	@mkdir -p $(@D)
	$(TOOL_C) $(CFLAGS_APP) $(LIB_INC) -c $< -o $@

# 硬件库不依赖 ABI 私有控制台; 探测结果交给调用方显示。
$(BUILD_DIR)/lib/kern/%.o: lib/src/%.c
	@mkdir -p $(@D)
	$(TOOL_C) $(CFLAGS_KERN) $(LIB_INC) -c $< -o $@
