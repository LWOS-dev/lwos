# 共享源码不决定运行时归属: APP 工具库与 KERN 硬件库分别编译。
# 显式列出源码, 避免新驱动自动进入每个应用。
APP_LIB_SRCS := app.c \
				string.c \
				convert.c \
				mem.c \
				fs.c \
				bitmap.c \
				gfx.c \
				gui.c \
				ui.c
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

$(BUILD_DIR)/tests/app_launch_test: tests/app_launch_test.c lib/src/app.c $(wildcard lib/include/*.h) lib/include/dev/blockdev.h
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror $(LIB_INC) tests/app_launch_test.c lib/src/app.c -o $@

test-app-launch: $(BUILD_DIR)/tests/app_launch_test
	$<
.PHONY: test-app-launch

$(BUILD_DIR)/lib/app/gui.o: resource/vgafont.h
