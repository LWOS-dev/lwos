# 当前代码与 spec2 对照概览

评估日期：2026-10-13。基线：`bd97848` 加当前工作区（包括已有的 `app/monitor/src/tui.c` 未提交修改）。

范围：只评估已有代码触及的 spec2 内容，依据源码、构建清单和调用路径；不编译、不运行测试、不启动模拟器。下文的“已实现”仅表示有对应实现，**不表示通过 spec 要求的运行验收**。参考实现 `*_reference.c` 不计入当前产品实现；没有实际实现的章节不展开。

## 总体判断

当前最扎实的实现是 **PIO 块设备与 FAT32 运行时读写**；其次是 **resman 启动骨架、GDT/IDT 接管、任务 0 TSS 和 IRQ1 输入**。PCI、图形、网络已有程度不同的实现，但与 spec2 的最终接口或当前可达入口仍有距离。LWP 完成了格式定义和初步打包，尚未形成重定位运行链。

不能按路线表直接说“已经完成 P几”：实际开发是交错的——P9 的 PIO 文件系统已经提前推进，P7 的中断基础已经开始，而 P2 的公开内存服务、P3 的 LWP 加载仍未闭合。

### 当前入口与 TUI 迁移

`app/monitor/src/monitor.c:monitor_main` 目前依次初始化 monitor IDT、内存池、文件系统、键盘和 FPU，自动调用 `start_resman`，然后进入 `tui_init/tui_loop`。

当前工作方向（用户明确）：将原命令行中的 demo 逐步迁入 TUI，最后弃用命令行。因此，命令行不可达本身不是需要修复的缺陷，也不建议为满足 spec 中旧命令名称而恢复命令行。后续应按同等能力在 TUI 中是否接入来评估迁移完成度。

`app/monitor/src/tui.c:tui_loop` 是无限循环，处理内存读写、转储面板和键盘。后面的 `prompt_read/execute` 命令循环不可达，而且 TUI 返回后还有停机循环。下表中的“旧命令路径不可达”统一表示“底层/旧 demo 代码保留，TUI 入口尚待迁移”，不否定已有底层实现。帮助文本中的 `rm` 也没有对应的 `execute` 分支；resman 实际已改为启动时自动进入。

### 资源归属：当前事实与整理方向

| 资源/状态 | 当前掌握者及交接关系 | 整理方向（不表示已实现） |
|---|---|---|
| TUI 面板、焦点、地址输入、编辑状态 | monitor；直接访问 VGA 文本显存 | demo 的操作与展示留在 monitor |
| 显示输出 | monitor TUI、resman 诊断绘制和 ABI 控制台均可写显存 | 明确前台显示及异常输出规则，避免把界面迁移误作 resman 的职责 |
| GDT、TSS | resman 安装 GDT 并维护 task0，当前 CPU 执行流仍运行 monitor | 系统描述符和后续任务生命周期由 resman 管理 |
| IDT、调试异常 | monitor 先安装初始 IDT；resman 接管后保留 monitor 的 1/3/8 入口 | resman 管表与注册，monitor 管调试上下文及处理逻辑；当前公开注册接口尚缺 |
| PIC、键盘 IRQ/队列 | resman 管 PIC、IRQ1、扫描码队列；ABI 解码并执行原输入 API；monitor 消费按键 | 保留底层输入与界面事件处理的区分 |
| 内存分配元数据 | monitor/resman 都链接分配器，指向同一固定 bitmap/pool；E820 副本及初始化状态各自独立 | 收拢到 resman 唯一初始化与分配，monitor 经服务申请和释放 |
| 栈与缓冲 | resman 初始化借 monitor 栈；ISR 使用当前执行栈；旧编辑器固定使用 0x400000 | 区分执行上下文与资源所有权，缓冲纳入分配/保留记录 |
| 磁盘设备 | ABI 管 ATA 驱动和 BLKDEV 注册表 | 若沿 spec2 收拢设备所有权，由 resman 管设备登记/生命周期，ABI 提供驱动 |
| 文件系统状态 | monitor 持有卷、句柄，静态链接 FS 实现；下层通过 ABI 的 BLKDEV 读写 | 界面状态留在 monitor；是否迁移 FS 服务按后续分层决定，不能描述成已经归 resman |
| 网络、图形 demo 状态 | 网络调试状态在 monitor；gfx/ui 为应用静态库，鼠标未进当前构建 | demo 迁入 TUI，同时消除固定缓冲与系统资源分配的冲突 |

以上是软件职责划分。当前全部 ring 0、平坦地址空间，monitor 能直接读写系统资源并不构成隔离或独占所有权。

## 对照表

| spec2 / 里程碑 | 当前能对上的内容 | 静态结论与边界 | 主要证据（文件:符号） |
|---|---|---|---|
| 10 内存布局 | ABI / resman / monitor 分别位于 1MB / 1.5MB / 2MB；有 E820 副本、固定元数据区域和低 4MB 保留 | 部分对齐。实际堆从 4MB 起，不是 spec 的 3MB；GDT/IDT 也占固定地址。映像边界检查还不完整 | `lib/src/mem.c:mmap_init,mem_pool_init`；`lib/src/task.c:gdt_init,idt_init`；三个映像的 `.ld`；`doc/memlayout.txt` |
| 20 / E1 PCI 枚举 | 扫描 256 条总线、32 个设备、多功能；打印 BDF、厂商/设备、分类和 IRQ；公共 PCI 头已存在 | 功能主体已有，但不是统一 `PCI_DEV` 表或独立枚举库；当前旧命令路径不可达 | `app/monitor/src/pci_det.c:pci_scan,pci_class_name`；`lib/include/pci.h`；`lib/src/pci.c` |
| 20 / E2 BAR | BAR 基址、类型、大小及部分 capability 打印 | 部分实现，存在直接偏离 spec 的探测逻辑，见下文 | `app/monitor/src/pci_det.c:bar_size,dump_bars,pci_detail` |
| 20 / E3 查找 | 有按 class/subclass 搜索 BDF 的函数 | 部分实现；无设备表查找、无 `pci_find_id` 和 prog-if 条件；多功能位读取有误 | `app/monitor/src/pci_det.c:pci_find_class` |
| 21 / AHCI | 正式文件有寄存器和命令常量，另有参考实现 | 仅准备阶段，A1 不计完成。`ahci.c` 无驱动函数且未进入 `KERN_LIB_SRCS`；参考文件不构成当前驱动 | `lib/src/dev/ahci.c`；`lib/src/dev/ahci_reference.c`；`lib/build.mk` |
| 22 / B1 PIO → BLKDEV | 有版本化 `BLKDEV`、注册表及 PIO read/write/flush 包装，ABI 导出取设备接口 | 核心实现已对齐。注册表归 ABI；`poll` 为空，采用同步操作。指定读 LBA0 demo 未验收 | `lib/include/dev/blockdev.h`；`lib/src/dev/ata.c:ata_register_blockdevs,pio_blk_read,pio_blk_write,pio_blk_flush`；`lib/abi/src/abi.c:disk_probe,lw_abi` |
| 22 / B2 FAT32 根目录 | 经 BLKDEV 读 MBR，选第一个 0x0B/0x0C 分区，挂载 FAT32、读目录项、列目录 | 核心实现已对齐，并接入启动挂载；目录命令留在旧命令处理器。仅支持 512B 扇区、每簇 1 扇区 | `lib/src/fs.c:fs_mount,fat_read_fte`；`app/monitor/src/fs_monitor.c:fs_init,dir_list` |
| 22 / B3 多级目录与文件读取 | 多段 8.3 路径查找、文件句柄、read/seek/tell/size/eof/close；有文件编辑器读入代码 | 核心实现已对齐，但不是 spec 的 `type /BOOT.INI` 入口；限制为短文件名，不支持 `.` / `..` 路径 | `lib/src/fs.c:lookup_impl,fs_file_open,fs_file_read`；`lib/include/fs.h`；`app/monitor/src/monitor.c:edit_hex,execute` |
| 22 写入扩展 | 文件覆盖/追加、分配和释放簇链、目录项操作、递归补父目录的 `fs_touch`、flush 同步；编辑器有保存路径 | 超出本章第一阶段只读目标。源码明确不保证失败原子回滚；这些实现不等于 B4（AHCI 替换）完成 | `lib/src/fs.c:fs_file_write,alloc_clusters,fs_touch,fs_sync`；`app/monitor/src/monitor.c:edit_hex` |
| 30/31 网络 N1、N3 的部分内容 | 临时 e1000 风格 RX 环、轮询收帧、LWFT 头识别；宿主工具能发送控制帧 | 仅接收/调试原型。无完整复位、MAC 获取、TX 环、NETDEV；不能据此认定某个型号的 N1–N3 全部完成 | `app/monitor/src/eth_tmp.c:eth_init,e1k_rx_poll,on_frame`；`tools/lwcon/lwcon.c:net_send` |
| 30 / N4–N5 的协议触及 | 有 LWFT magic、op/len/seq/arg 和 RUN 分支 | 只有协议外形。RUN 实际执行内存读写调试命令，不是 LWP 运行；未实现 PUT/DATA/END/ACK/CRC 的传文件闭环 | `app/monitor/src/eth_tmp.c:on_frame`；`app/monitor/include/eth_tmp.h` |
| 40 / X0 LWP 格式 | `LwpHdr` 与 spec 的 0x40 字节 v1 头一致，修正 image/mem/stack 含义，有大小静态断言 | 格式定义已对齐 | `lib/include/lwp.h:LwpHdr`；`tools/mkexe/doc.txt` |
| 40 / X1 ELF → LWP | 主工具读取 PT_LOAD、拼映像、计算文件/内存大小并输出头和映像，构建规则已接入 | 部分实现。入口固定 0，重定位数固定 0，`abi_need` 未填写；`-d` 解析的是 ELF 而非 LWP，不能算 X1 完成 | `tools/mkexe/mkexe.c:main,write_file`；`tools/mkexe/build.mk` |
| 40 / X2 加载器接口 | 有 `LW_ENV`、映像句柄声明、load/run/unload slot 编号 | 占位。服务表没有填这些函数，`image.c` 只有 include，且未进 resman 构建。不能据此计入加载或双地址重定位运行 | `lib/include/resman.h`；`app/resman/src/head.c:lw_rm`；`app/resman/src/image.c`；`app/resman/build.mk` |
| 50 / R0 resman 骨架 | 预加载固定映像、LWRM 身份、版本函数、monitor 校验头并清 BSS、调用入口后返回 | 骨架已实现；采用 monitor 自动调用、共用栈，未走 spec 的 ATTACH RESMAN 路线；版本接口返回 1，未形成所述打印身份/slot 数的 demo | `loader/boot.ini`；`app/monitor/src/monitor.c:start_resman`；`app/resman/src/head.c:lw_rm`；`app/resman/src/resman.c:resman_main,resman_version` |
| 50 / R1 E820 与空闲区 | E820 复制到静态表，bitmap 标识可用页，保留低 4MB | 部分实现；有 E820 dump，但没有统一的 resman 空闲块查询/展示；monitor 和 resman 仍各自链接同一套内存代码 | `lib/src/mem.c:mmap_init,mem_pool_init,mmap_dump`；`app/resman/build.mk`；`lib/build.mk` |
| 50 / R2 分配释放 | 页粒度连续分配及 bitmap 释放，连续空闲页可再次一起分配 | 内部能力已有，公开 `rm_alloc/rm_free` slot 仍为空；无显式 align 参数、tag/owner，不能算 R2 公共服务完成 | `lib/src/mem.c:kmalloc,kfree,mem_alloc_units`；`app/resman/src/head.c:lw_rm` |
| 50 / R4 设备表的部分内容 | ABI 中已有块设备注册和索引查询 | 部分概念落地，尚不是 resman 扫 PCI → 统一 bind → 分类设备表；不能算 R4 完成 | `lib/src/dev/blockdev.c`；`lib/abi/src/abi.c:disk_probe` |
| 51 / T0 GDT、IDT 接管 | resman 安装新 GDT、256 项 IDT、共用 ISR 和 C 分发；保留 monitor 的 1/3/8 入口 | 主体已实现，但采用继承旧门方式，无公开 `rm_set_vector`；单步、断点、BIOS 跳板行为未验收 | `lib/src/task.c:gdt_init,idt_init`；`app/resman/src/isr.s`；`app/resman/src/interrupt.c:resman_isr_set_handler,resman_isr_dispatch` |
| 51 / T1 的任务 0 部分 | TSS32/TASK、TSS 描述符、task0、`ltr` | 仅基础部分；没有任务创建、yield、切换和 A/B demo | `lib/include/task.h`；`lib/src/task.c:bootstrap_tss_init,task_register_load` |
| 51 / T2 的 PIC 部分，以及 D5 输入临界区 | PIC 重映射 0x20/0x28、IRQ1 扫描码队列、EOI、绑定 ABI 输入源；入队/读队列和命令事务有中断保护 | IRQ 键盘调用链已接入。没有 PIT/tick，不能算 T2；局部输入锁也不能算 T6 全 ABI 锁审计完成 | `app/resman/src/interrupt.c:pic_init,pic_irq_set`；`app/resman/src/keyboard.c`；`lib/abi/src/kbd.c` |
| 60 / G0–G2 | pitch 寻址、矩形/文字、8×16 字体、后备缓冲指针、脏矩形刷新 | 图形基础代码已有并链接到 monitor；当前 monitor 没有调用这些绘制函数或建立 G0–G2 demo。使用全局 fb/bb/pitch，没有 spec 的显式画布对象 | `lib/src/gfx.c`；`lib/include/gfx.h`；`lib/build.mk`；`loader/boot.ini` |
| 60 / G3–G4 的部分内容 | PS/2 鼠标初始化、解包和轮询；单个矩形拖动逻辑 | 分离的代码能力。`mouse.c` 未进入当前构建，未找到当前图形光标调用链；拖动矩形不等于窗口链表、z 序和置顶 | `lib/src/mouse.c`；`lib/src/ui.c:drag_mouse`；`lib/build.mk` |

## 影响“已完成”判断的具体差距

1. **内存布局和防重叠检查没有统一。** `lib/abi/abi.ld` 允许 ABI 的 BSS 延伸到 `0x200000`，但 resman 已从 `0x180000` 开始；`app/monitor/monitor.ld` 没有终点 ASSERT。stage2 的 loader 栈仍设为 `0xA0000`，spec 10 提到的 EBDA 风险尚未处理。这里只判断防护缺口，不推断当前二进制已经重叠。
2. **分配器还不是系统唯一资源所有者。** `mem_pool_init` 会重置固定 bitmap/pool；两份链接副本共享物理元数据却各有初始化状态。当前 `edit_hex` 又把 `0x400000` 当固定缓冲，这正是页分配器开始可分配的区域。后续恢复编辑器入口并增加动态分配时，需要统一保留/分配关系，才能符合 spec 50。
3. **PCI E2 的安全步骤缺失。** `bar_size` 写全 1 探测前未关闭 COMMAND 的 IO/Memory decode；`pci_detail` 未按 header type 限制 BAR 数量；64 位 BAR 只探测低半部，未完整联合测量。E3 的 `pci_find_class` 从配置 DWORD `0x0C` 右移 8 取 header type，实际应是右移 16，因此可能漏扫多功能设备。
4. **LWP 当前还不能承担任意地址运行。** 主工具没有收集 `R_386_32`、处理未定义符号或输出重定位表；运行端也没有实际加载服务。参考工具代码、头文件声明和生成了 `.exe` 文件都不能替代 X1–X3 的完成证据。
5. **网络实现仍是调试通路。** 目标端没有发送/ACK 链；寄存器和 DMA 状态访问使用普通指针；`on_frame` 忽略实际帧长并按 op 索引字符串表，缺少长度和操作码边界检查。它与 spec 的可靠传文件协议还有实质距离，也没有足够型号专用实现可以给 82540EM/82567LM 判定完成。
6. **图形代码与 spec 的接口、入口都未闭合。** `set_info` 忽略 bpp，没有 RGB 掩码检查；前后缓冲共用 pitch，未实现两个画布各自 pitch；没有当前可达的光标/窗口 demo。当前 TUI 直接写文本显存，不属于 G5 所说的图形文本窗口和控制台重定向。

## 横切决策的实际落点

- **D1 平坦段：对齐。** 新 GDT 保留平坦 32 位代码/数据段及 BIOS 跳板的 16 位段。
- **D2 resman 内核、ABI HAL：部分对齐。** resman 虽仍在 `app/`，构建已明确用 `CFLAGS_KERN`，目录名并未导致 APP 编译选项误用；GDT/IDT/PIC 已归 resman。文件系统仍静态链接进 monitor，设备注册表在 ABI，内存归属未统一。
- **D3 统一绑定入口：未落地到已有设备实现。** 当前是 ATA 专用探测和 BLKDEV 注册，未采用 `lw_drv_bind`。
- **D4 轮询优先：设备路径基本沿用，键盘中断提前接入。** 当前没有由 PIT 驱动的任务调度。
- **D5/D6：只有局部基础。** 键盘队列有临界区，KERN 构建禁用浮点；PCI/控制台等尚未完成统一锁审计，没有多任务 FPU 惰性保存。
- **D7：固定映像已有，LWP 尚在工具阶段。** DLL 仅见格式预留，不计 DLL 功能完成。

## 建议登记的进度口径

- 可以登记为“核心代码已有”：**X0；B1–B3 的 PIO/只读 FS 主体及额外写入能力；R0 骨架；T0 描述符接管主体；IRQ1 输入链；图形基础函数**。同时保留上述接口、格式限制和入口说明。
- 应登记为“部分实现”：**E1–E3、R1–R2、X1、T1 的 task0、T2 的 PIC、G0–G4 的局部能力、网络收帧原型**。
- 不应把空 slot、参考文件或旧命令处理器当作已经完成的运行链。尤其是 **resman 公开分配 → LWP 重定位加载 → 新任务运行**，当前还没有接通。

spec2 多处“现状”已经滞后：22 仍写运行时没有 FS，30 写没有网络代码，40 写只有头草稿；50 仍写 `rm` 命令启动；51 同时存在“尚未安装 TSS”和“已有任务 0 TSS”的表述。后续同步现状时，应以本次查到的实际构建和启动路径为准，运行验收状态仍单独记录。

本次文档更新包含此概览及根目录 README；没有修改实现或 spec，没有运行任何测试。
