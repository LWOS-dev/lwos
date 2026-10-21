# 99 — 开发日志

新的写在最上面。格式见 [90-roadmap.md](90-roadmap.md)。

---

## 2026-10-21  文本窗口 / stdio 需求定稿

- 做到：新增 [61](61-text-console-stdio.md)，明确 TEXT flag → resman 字符缓冲区 → env → GHOST 绘制 → CRT/printf 的目标链路；同步 EXE、GUI、索引和路线。此次仅更新规格，未实现运行时代码。
- 下一步：C0，TEXTDEMO 直接写 80×25 字符缓冲区，由 GHOST 绘制窗口。
- 坑：TEXT 已定义为 0x10；任务回收不能释放仍被窗口读取的文本对象；Hello World 正常退出后须保留输出。

## 2026-10-13  IRQ 键盘 / ABI 后端

- 做到：IRQ1 收扫描码进环形队列，原 ABI getk/getc/getp 绑定此来源；空闲 HLT、LED ACK 与扫描码分离、ABI 再 attach 保留后端。make、宿主解码和 QEMU PC/Q35 键盘回归通过。
- 下一步：多任务时将阻塞 getk 接任务等待队列；统一 PS/2 鼠标和键盘命令管理，再推进 PIT 与调度。
- 坑：IRQ 中不解码/绘图；getk 在 IF=1 前台调用；原 getp 空输入继续返回 0xffffffff，队列溢出由 kb_dropped 计数。

## 2026-10-13  T0 / RESMAN 共用 ISR

- 做到：256 个向量小入口接共用 stub，规范错误码/寄存器帧并对齐 C 调用栈；IDT 接管时保留 monitor 单步/断点和向量 8，提供私有回调注册。make、QEMU PC/Q35 的 #UD/#GP 与中断返回回归通过。
- 下一步：完成具体 IRQ 分发、PIC 重映射与键盘队列；再推进协作式任务切换。
- 坑：当前 RESMAN 蓝底循环不返回，旧命令行 boot_test 不适用；普通 IRQ 仍屏蔽，专用 #DF 初始化暂不调用。

## 2026-10-12  RESMAN 清理 / T0 准备

- 做到：移除 RESMAN 图形/鼠标测试与 GDT 临时输出，只保留一次性内存/GDT 初始化；修正 GDTR 字段顺序及六字节布局，更新启动回归和当前进度说明；make 与 QEMU PC/Q35 启动、重复调用、GDT、视频切换回归通过。
- 下一步：统一分配器所有权并修正释放；T0 接管 IDT，然后以内置测试函数实现 T1 的 TSS/协作式 yield。
- 坑：现有段编码器不能直接用于 TSS；EXE 装载槽仍为空；monitor 编辑器的 `0x400000` 固定缓冲与动态分配区重叠。

## 2026-09-30  spec2 建立

- 做到：spec2 初稿（00–90）。ABI 已有 pitch slot（0x34）和 PCI slot（0x40–0x42）
- 下一步：P1 —— GUI G0（`lib/gfx` 的画布 + fill_rect）或 PCI E1（`pci_def.h` + `pl` 命令），哪个有灵感先做哪个
- 坑：画曲线 demo 的 `acc[sy]` 没检查下标，越界写把栈踩坏，Bochs 日志里的表现是一串 GDT index 越界。以后看到这种日志，先找最近写过的数组
