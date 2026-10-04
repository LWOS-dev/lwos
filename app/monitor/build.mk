# app/monitor: MONITOR.BIN, 固定在 200000H, 控制台全部走 ABI. APP 层

MON_DIR		= app/monitor
MON_ELF		= $(BIN_DIR)/monitor.elf
MON_BIN		= $(BIN_DIR)/monitor.bin
MON_OBJS	= $(BUILD_DIR)/$(MON_DIR)/head.o \
			  $(BUILD_DIR)/$(MON_DIR)/monitor.o \
			  $(BUILD_DIR)/$(MON_DIR)/isr.o \
			  $(BUILD_DIR)/$(MON_DIR)/idt.o \
			  $(BUILD_DIR)/$(MON_DIR)/pci_det.o \
			  $(BUILD_DIR)/$(MON_DIR)/eth_tmp.o \
			  $(BUILD_DIR)/$(MON_DIR)/fs_monitor.o

$(BUILD_DIR)/$(MON_DIR)/%.o: $(MON_DIR)/src/%.c
	@mkdir -p $(@D)
	$(TOOL_C) $(CFLAGS_APP) $(LIB_INC) -I$(MON_DIR)/include -c $< -o $@

$(BUILD_DIR)/$(MON_DIR)/%.o: $(MON_DIR)/src/%.s
	@mkdir -p $(@D)
	$(TOOL_ASM) $(ASFLAGS) $< -o $@

# libgcc 必须在所有 .o 之后
$(MON_ELF): $(MON_OBJS) $(APP_LIB_OBJS) $(MON_DIR)/monitor.ld
	@mkdir -p $(@D)
	$(TOOL_LD) $(LDFLAGS) -T $(MON_DIR)/monitor.ld -o $@ \
		$(MON_OBJS) $(APP_LIB_OBJS) $(LIBGCC)

FSROOT_FILES += $(FSROOT)/MONITOR.BIN
$(FSROOT)/MONITOR.BIN: $(MON_BIN)
