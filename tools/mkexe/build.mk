# 根构建通过宿主工具 Makefile 生成测试 EXE, 再交给通用 fsroot 复制规则。
MKEXE_TEST_EXE = $(BUILD_DIR)/tools/mkexe-test/test.exe
MKEXE_INPUTS = tools/mkexe/Makefile tools/mkexe/build.mk \
    tools/mkexe/mkexe.c tools/mkexe/mkexe.h tools/mkexe/hdr.h \
    tools/mkexe/test.c tools/mkexe/lwp.ld \
    lib/include/lwp.h lib/include/abi.h lib/include/stdint.h \
    lib/include/dev/blockdev.h

$(MKEXE_TEST_EXE): $(MKEXE_INPUTS)
	$(MAKE) -C tools/mkexe test-exe \
	    TARGET=$(abspath $(BUILD_DIR)/tools/mkexe) \
	    TEST_DIR=$(abspath $(BUILD_DIR)/tools/mkexe-test)

FSROOT_FILES += $(FSROOT)/TEST.EXE
$(FSROOT)/TEST.EXE: $(MKEXE_TEST_EXE)
