# loader/: LOADER.BIN, 0x10000. KERN 层

LOADER_ELF	= $(BIN_DIR)/loader.elf
LOADER_BIN	= $(BIN_DIR)/loader.bin
LOADER_OBJS	= $(BUILD_DIR)/loader/loader.o $(BUILD_DIR)/loader/bootdisk.o

$(BUILD_DIR)/loader/%.o: loader/%.c
	@mkdir -p $(@D)
	$(TOOL_C) $(CFLAGS_KERN) $(LIB_INC) -c $< -o $@

$(LOADER_ELF): $(LOADER_OBJS) loader/loader.ld
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) -T loader/loader.ld -o $@ $(LOADER_OBJS)

FSROOT_FILES += $(FSROOT)/LOADER.BIN $(FSROOT)/BOOT.INI
$(FSROOT)/LOADER.BIN: $(LOADER_BIN)
$(FSROOT)/BOOT.INI:   loader/boot.ini
