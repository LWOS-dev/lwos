# RESMAN 当前进度

`make` 生成 `RESMAN.BIN`，由 `loader/boot.ini` 预加载到 `0x180000`。
monitor 启动时调用 RESMAN：首次清 BSS，初始化内存页分配器、GDT、
任务 0 TSS 和 IDT。当前 `resman_main()` 随后进入键盘按键码显示循环，不返回 monitor。
仍沿用 monitor 的栈。
图形、鼠标、拖动矩形测试已从 RESMAN 移除；共享 gfx/ui/mouse 库继续保留供应用使用。

## 已实现

- 映像头、身份标识、版本 slot 和 monitor 的调用/返回路径。
- `mmap_init()` 将 BIOS E820 表复制到本映像的 BSS。
- 复用 `lib/src/mem.c` 的页位图和页分配器，低 4 MiB 不参与动态分配。
- `app/resman/src/task.c` 在 `0x360000` 建立 GDT，保留与 stage2 一致的前五项。
  GDTR 使用 packed 的六字节硬件格式（limit 在前，base 在后）。
- 任务表、当前任务、GDT/IDT 状态和初始化归 RESMAN 所有，私有声明在 `include/task.h`。
  `lib/src/task.c` 只保留无状态的描述符编解码。映像头 `TASK_TABLE` 槽保存数组首地址，
  `TASK_CURRENT` 槽保存 `current_task` 指针变量的地址；Monitor 通过槽位读取共享状态。
- `lib/include/task.h` 已定义 104 字节 TSS32、TASK 和任务状态。
- GDT/IDT 描述符使用带布局注释的 packed 结构体；原 `lib/include/idt.h`
  和 `lib/src/idt.c` 已合并到 task。段编解码包含 access 和属性四位。
- `idt_init(resman_isr_table)` 在 `0x370000` 构建 256 项 IDT，然后装载六字节 IDTR。
  保留 monitor 的向量 1/3 调试入口和向量 8，其余向量接入 RESMAN 的共用 stub。
- `src/isr.s` 的小入口统一 vector/error，保存通用及段寄存器、清 DF，
  对齐 C 调用栈；分发后恢复寄存器、丢弃 vector/error 并 `iretd`。
- `include/isr.h` 定义 ring 0 的 ISR_FRAME；`resman_isr_set_handler()` 注册
  私有回调，未注册的向量打印 VECTOR/ERROR/EIP 后停机。
- PIC 已重映射至 `0x20/0x28`，只开启 IRQ1。`src/keyboard.c` 收扫描码并 EOI，
  使用 128 字节环形队列（最多存 127 字节），溢出计入 `kb_dropped`。
- ABI 新增 `0x2c` 后端绑定 slot；应用的 `lw_getk/getc/getp` 调用不变。
  ABI 保留原扫描码解码，取字节时改用 RESMAN 队列；getp 非阻塞返回，
  getk 在空队列时用原子检查与 `sti; hlt` 等待。绑定前仍可启动轮询。
- LED 命令在前台有限等待并关闭中断，收 ACK 期间的扫描码仍入队；
  IRQ 中不绘图、不解码、不发送命令。ABI 再次 attach 不清共享 BSS/后端。
- 当前执行流已安装任务 0 TSS 并 `ltr`；本轮不启用专用 #DF 任务门或应急栈，
  旧 double fault 源码保留但初始化不调用。

## 尚未实现

- `rm_alloc/free` 服务：只有声明，表槽为 null；旧分配器还没有 owner/tag/对齐接口。
- `image_load/run/unload`：只有声明和 RM_IMAGE 结构，`src/image.c` 是占位文件且未参与构建。
- 面向 APP 的异常/IRQ 注册服务、具体 IRQ 处理、任务创建/切换启动桩和 yield。
- PIT 时钟、抢占、sleep、任务退出回收和 FPU 上下文管理。

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
   初期只开启键盘 IRQ，不使用浮点；切走后才能回收 DEAD 任务的栈/TSS。
4. 单独验证 PIC/PIT tick，再接抢占、sleep、FPU 保存和共享 ABI 状态保护。
5. 完成 EXE 装载接口，把独立编辑器挂到任务创建路径；输入和显示也需统一管理。

具体设计及验收见 `spec2/50-resman-memory.md` 和 `spec2/51-tasks.md`。
这些文档中的路线是目标，以上清单描述当前实现。

## 验证

当前构建可通过。共用 ISR 的模拟器回归：

```sh
make
python3 tests/resman_isr_test.py
python3 tests/keyboard_irq_test.py
```

QEMU PC/Q35 验证 256 项 IDT、保留的调试入口、#UD/#GP 错误码及 EIP、
高向量软件中断的返回、寄存器/方向标志恢复和 C 调用栈对齐。
键盘回归覆盖实际 IRQ/EOI、F7/方向键/修饰键/Caps LEDs、队列回绕、空闲 HLT，
以及重新 attach 后的原 ABI 输入 slot。阻塞输入用于 IF=1 的单前台，
多任务等待队列及统一 PS/2 鼠标命令管理尚未实现。
旧 `boot_test.py` 依赖命令行调用返回和专用 #DF，暂不适用于当前循环。

描述符宿主测试（不执行特权指令）：

```sh
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fno-builtin -ffunction-sections -fdata-sections -Wl,--gc-sections -iquote lib/include tests/task_descriptor_test.c lib/src/task.c -o /tmp/lwcnc-task-descriptor-test
/tmp/lwcnc-task-descriptor-test
```

- `make`
- `python3 tests/boot_test.py`：QEMU PC/Q35 启动、重复调用/返回 RESMAN、
  GDTR 格式及 CPU 实际 GDT、视频跳板进入/退出。
- `tests/mouse_test.c` 仍用于共享鼠标库测试，不代表 RESMAN 提供 GUI。
