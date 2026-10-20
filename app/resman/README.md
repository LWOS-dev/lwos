# RESMAN 当前进度

`make` 生成 `RESMAN.BIN`，由 `loader/boot.ini` 预加载到 `0x180000`。
monitor 启动时调用 RESMAN：首次清 BSS，初始化内存页分配器、GDT、
任务 0 TSS、IDT、idle 和键盘/PIT，再自动装载 `/GHOST.EXE`。
resman 主入口仍沿用 monitor 的栈；ghost 使用独立任务栈。
图形由 ghost 持有；共享 gfx/ui/mouse 库继续保留供应用使用。

## GHOST 自动启动

`src/autoload.c` 在首次初始化时使用 ABI 已注册的块设备 0，挂载自己的
临时 FAT32 卷，读取 `/GHOST.EXE`，调用 image_load 完成校验和重定位。
不重复探测磁盘，不修改 monitor 的卷状态。文件缓存、句柄和失败时尚未绑定的
映像会释放；任务创建成功后，映像和栈交给任务生命周期管理。

创建 READY 任务、设置名称 `GHOST.EXE`、交接键盘在同一个关中断区间完成。
启用中断后，monitor 在 resman 中等待键盘归还，期间处于 WAITING，
因此不会初始化文本 TUI 或抢走 ghost 的按键。无任务可运行时进入 idle。
ghost 退出或被 kill 后，键盘恢复给任务 0，丢弃旧 UI 的积压按键并唤醒 monitor；
monitor 返回文本模式后继续原来的启动流程。当前 ghost 按 Esc 正常退出，
随后按任意键通过 monitor 原有的启动提示，即可使用文件浏览器。

自动装载只尝试一次。缺少文件、读取失败、映像损坏或创建失败时直接回退 monitor，
启动输出包含失败阶段和原始错误码；`ghost_autoload_result` 保留结果供调试器查看。
成功退出的 ghost 保留 DEAD、退出码和映像，之后可在 TASKS 中按 F9 回收。

构建和回归：

```sh
make
python3 -B tests/ghost_autoload_test.py
```

回归在 QEMU PC/Q35 上检查真实图形 framebuffer 写入、键盘独占、Esc 退出、
程序立即返回、缺少文件、无效映像，以及 monitor 恢复和 idle 调度。

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

- `rm_alloc/free` 已实现，详见下文“应用内存分配”；其余旧进度条目待整理。
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

## 应用内存分配

`src/alloc.c` 将已预留的 ALLOC/FREE 槽接到 kmalloc/kfree。允许 size 为
1..0x7ffff000，align 为 0 或不超过 4096 的二次幂；实际按 4 KiB 页分配，
内容未初始化，失败统一返回 NULL。tag 原样记录，不影响分配行为。

分配记录保存在 RESMAN 的私有表（16384 项），保存指针、任务 ID 和 tag。
不改变 0x320000 处共享 MEM_POOL 的布局。只有 rm_alloc 分配进入这张表，
映像和栈仍按原路径管理。rm_free(NULL) 成功；非起始地址、重复释放、其他任务的
分配和内核分配均返回 RM_ERR_ARGUMENT。此处仅做资源归属检查，没有地址空间隔离。

底层 kmalloc/kfree/mem_alloc_units 及服务层的记录更新都保存 IF、关中断，完成后
恢复原 IF；嵌套调用不会意外开中断。宿主回归编译不执行特权指令，OS 的 freestanding
构建执行真实 IRQ 临界区。初始化仍必须在启动阶段无并发调用。

后台任务自然返回、主动 exit 或被 kill 后，额外分配保留到 reap，再统一释放；
释放记录后任务 ID 才可复用。F4 程序运行在任务 0，需显式 rm_free，返回不自动回收。
`make test-alloc` 检查底层页分配；`python3 tests/task_schedule_test.py` 检查真实
QEMU 多任务分配、IF 恢复和退出/终止后的回收。
