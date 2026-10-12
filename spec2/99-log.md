# 99 — 开发日志

新的写在最上面。格式见 [90-roadmap.md](90-roadmap.md)。

---

## 2026-10-12  RESMAN 清理 / T0 准备

- 做到：移除 RESMAN 图形/鼠标测试与 GDT 临时输出，只保留一次性内存/GDT 初始化；修正 GDTR 字段顺序及六字节布局，更新启动回归和当前进度说明；make 与 QEMU PC/Q35 启动、重复调用、GDT、视频切换回归通过。
- 下一步：统一分配器所有权并修正释放；T0 接管 IDT，然后以内置测试函数实现 T1 的 TSS/协作式 yield。
- 坑：现有段编码器不能直接用于 TSS；EXE 装载槽仍为空；monitor 编辑器的 `0x400000` 固定缓冲与动态分配区重叠。

## 2026-09-30  spec2 建立

- 做到：spec2 初稿（00–90）。ABI 已有 pitch slot（0x34）和 PCI slot（0x40–0x42）
- 下一步：P1 —— GUI G0（`lib/gfx` 的画布 + fill_rect）或 PCI E1（`pci_def.h` + `pl` 命令），哪个有灵感先做哪个
- 坑：画曲线 demo 的 `acc[sy]` 没检查下标，越界写把栈踩坏，Bochs 日志里的表现是一串 GDT index 越界。以后看到这种日志，先找最近写过的数组
