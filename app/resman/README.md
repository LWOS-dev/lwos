# RESMAN 当前进度

`make` 生成 `RESMAN.BIN`，由 `loader/boot.ini` 预加载到 `0x180000`。
在 monitor 输入 `rm`：首次清 BSS，调用 RESMAN，初始化内存页分配器和 GDT，
返回 monitor；再次调用保留初始化状态。当前沿用 monitor 的栈；IDT 接管草稿已加入，但尚未完成启动验证。
图形、鼠标、拖动矩形测试已从 RESMAN 移除；共享 gfx/ui/mouse 库继续保留供应用使用。

## 已实现

- 映像头、身份标识、版本 slot 和 monitor 的调用/返回路径。
- `mmap_init()` 将 BIOS E820 表复制到本映像的 BSS。
- 复用 `lib/src/mem.c` 的页位图和页分配器，低 4 MiB 不参与动态分配。
- `lib/src/task.c` 在 `0x360000` 建立 GDT，保留与 stage2 一致的前五项。
  GDTR 使用 packed 的六字节硬件格式（limit 在前，base 在后）。
- `lib/include/task.h` 已定义 104 字节 TSS32、TASK 和任务状态。
- GDT/IDT 描述符使用带布局注释的 packed 结构体；原 `lib/include/idt.h`
  和 `lib/src/idt.c` 已合并到 task。段编解码包含 access 和属性四位。
- `idt_init()` 在 `0x370000` 复制当前 IDT 的有效入口，再用六字节 IDTR 装载；
  保留 monitor 处理函数，尚未提供 RESMAN 自己的异常/IRQ 处理程序。

## 尚未实现

- `rm_alloc/free` 服务：只有声明，表槽为 null；旧分配器还没有 owner/tag/对齐接口。
- `image_load/run/unload`：只有声明和 RM_IMAGE 结构，`src/image.c` 是占位文件且未参与构建。
- RESMAN 自己的 IDT、异常/IRQ 注册、任务表、TSS 描述符、`ltr`、切换启动桩、yield。
- PIC 重映射、PIT 时钟、抢占、sleep、任务退出回收和 FPU 上下文管理。

## 继续顺序

1. 内存基础：让 RESMAN 成为分配器的唯一所有者，修正旧 `kfree` 未清空池记录、
   接受块内地址以及 E820 页边界处理等问题；实现可验证的 alloc/free。
   monitor 当前也初始化同一组固定地址元数据，编辑器固定缓冲区 `0x400000`
   又与动态分配区重叠，迁移时必须一起处理。为任务记录资源 owner。
2. T0：接管 IDT，保留 monitor 的单步/断点处理；明确 GDT 和异常处理的所有权。
   `seg_write` 默认 G=1、D/B=1；TSS 使用 `seg_xlat_in`，指定
   `.flag=GDT_ACC_TSS32_AVAIL`、`.attr=0`、`.limit=103`。
3. T1：先用两个内置整数测试函数，不依赖 EXE 装载器。建立任务表，为任务 0
   安装 TSS 并 `ltr`；新任务分配独立栈，由启动桩调用入口，再用远 `jmp`
   和显式 yield 交替打印 A/B，最后能回到 monitor。
   初期保持 IRQ 屏蔽，不使用浮点；切走后才能回收 DEAD 任务的栈/TSS。
4. 单独验证 PIC/PIT tick，再接抢占、sleep、FPU 保存和共享 ABI 状态保护。
5. 完成 EXE 装载接口，把独立编辑器挂到任务创建路径；输入和显示也需统一管理。

具体设计及验收见 `spec2/50-resman-memory.md` 和 `spec2/51-tasks.md`。
这些文档中的路线是目标，以上清单描述当前实现。

## 验证

当前完整 `make` 会停在 `double_fault_init()` 和 `task_regsiter_load()`：
两者仅有调用，没有声明或实现。补齐后再运行启动回归；此前启动测试通过
不代表当前初始化草稿已完成。

描述符宿主测试（不执行特权指令）：

```sh
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fno-builtin -ffunction-sections -fdata-sections -Wl,--gc-sections -iquote lib/include tests/task_descriptor_test.c lib/src/task.c -o /tmp/lwcnc-task-descriptor-test
/tmp/lwcnc-task-descriptor-test
```

- `make`
- `python3 tests/boot_test.py`：QEMU PC/Q35 启动、重复调用/返回 RESMAN、
  GDTR 格式及 CPU 实际 GDT、视频跳板进入/退出。
- `tests/mouse_test.c` 仍用于共享鼠标库测试，不代表 RESMAN 提供 GUI。
