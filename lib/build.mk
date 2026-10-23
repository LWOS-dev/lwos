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

# Opt-in text runtime. Do not link CRT/stdout into monitor or legacy programs.
STDIO_LIB_OBJS := $(addprefix $(BUILD_DIR)/lib/app/,vsprintf.o stdio.o)
CRT_OBJS := $(BUILD_DIR)/lib/app/crt0.o

STDIO_TEST_NAMES := vsnprintf snprintf vsprintf sprintf vfprintf fprintf vprintf printf \
    fputc putchar fputs puts stdout stdio_init stdio_set_cursor stdio_get_cursor stdio_set_attr
STDIO_TEST_FLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror -fno-builtin \
    -fsanitize=address,undefined $(LIB_INC)
STDIO_TEST_DEFS := $(foreach name,$(STDIO_TEST_NAMES),-D$(name)=test_$(name))
$(BUILD_DIR)/tests/stdio_second.o: lib/src/stdio.c lib/src/format.h $(wildcard lib/include/*.h)
	@mkdir -p $(@D)
	$(TOOL_C) $(STDIO_TEST_FLAGS) $(foreach name,$(STDIO_TEST_NAMES),-D$(name)=second_$(name)) -c $< -o $@
$(BUILD_DIR)/tests/stdio_test: tests/stdio_test.c lib/src/stdio.c lib/src/vsprintf.c \
    lib/src/format.h $(wildcard lib/include/*.h) $(BUILD_DIR)/tests/stdio_second.o
	$(TOOL_C) $(STDIO_TEST_FLAGS) $(STDIO_TEST_DEFS) tests/stdio_test.c \
	    lib/src/stdio.c lib/src/vsprintf.c $(BUILD_DIR)/tests/stdio_second.o -o $@
test-stdio: $(BUILD_DIR)/tests/stdio_test
	$<
.PHONY: test-stdio

$(BUILD_DIR)/tests/convert_test: tests/convert_test.c lib/src/convert.c lib/src/string.c lib/include/convert.h lib/include/string.h lib/include/stdint.h
	@mkdir -p $(@D)
	$(TOOL_C) -std=gnu11 -Wall -Wextra -Werror -Wno-return-type -fno-builtin \
	    $(LIB_INC) tests/convert_test.c lib/src/convert.c lib/src/string.c -o $@

test-convert: $(BUILD_DIR)/tests/convert_test
	$<
.PHONY: test-convert
