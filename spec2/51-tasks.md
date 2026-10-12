# 51 — 任务切换（GDT + TSS，无分页）

## 目标

多个任务轮流运行：先协作式（主动 yield），再抢占式（时钟中断）。
使用 **x86 硬件任务切换**：每个任务在 GDT 里有一个 TSS 描述符，`ljmp` 到这个选择子就完成切换。

## 现状

- stage2 提供初始 GDT；RESMAN 首次进入时在 `0x360000` 建立并安装新 GDT，前五项与 stage2 一致，尚未安装 TSS。
- `lib/include/task.h` 已有 TASK/TSS32 定义；创建、yield、退出和调度尚未实现。
- IDT 在 monitor 里；PIC 未重映射且全部屏蔽；没有时钟。
- 所有代码都在 ring 0。

## 依赖

- 前置：[50 resman](50-resman-memory.md) R2（分配栈和 TSS）；[02](02-decisions.md) D2（IDT/GDT 归 resman）。
- 后继：[40](40-lwp-exe.md) EXE 作为任务运行；[60](60-gui.md) G6 每任务一个窗口。
- 会影响到的既有代码：**ABI 里所有带共享状态的函数**（D5 加锁）、monitor 的调试功能（IDT 换主人）。

## 设计

### 特权级：全部 ring 0 〔荐〕

和 [02](02-decisions.md) D1 一致，不做隔离。后果：

- 中断发生时**不切栈**，ISR 直接用当前任务的栈。**每个任务栈都要给 ISR 留余量**（建议至少 4KB）。
- TSS 里的 `ss0/esp0` 用不上，但要填合法值。

### 新 GDT（resman 的 bss）

| 索引 | 选择子 | 内容 |
|---|---|---|
| 0 | 0x00 | NULL |
| 1 | 0x08 | 32 位平坦代码 |
| 2 | 0x10 | 32 位平坦数据 |
| 3 | 0x18 | 16 位代码（跳板用） |
| 4 | 0x20 | 16 位数据（跳板用） |
| 5… | 0x28… | TSS 描述符，每个任务一个（例如最多 64 个） |

**前 5 项必须和 stage2 完全一致**：BIOS 跳板在调用期间会切回 stage2 自己的 GDT，然后用
`sgdt` 保存的值恢复。选择子一样，才能保证跳板进出时段寄存器都还有效。

TSS 描述符：type = 0x9（可用的 32 位 TSS），limit = 103，base = TSS 地址。
切换后硬件会把它改成 0xB（忙）。

### TSS 和任务结构

```c
typedef struct {                 /* 硬件格式，104 字节 */
    DWORD link, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
    DWORD eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    DWORD es, cs, ss, ds, fs, gs, ldt;
    WORD  trap, iomap;           /* iomap = 104，表示没有 IO 位图 */
} TSS;

typedef struct {
    TSS   tss;
    WORD  id, sel;               /* 任务号，TSS 选择子 */
    BYTE  state;                 /* FREE / READY / SLEEP / DEAD */
    DWORD wake_tick;
    PVOID stack;
    BYTE  fpu[108];              /* fsave 区 */
    BYTE  fpu_used;
} TASK;
```

`cr3` 填 0 即可：没开分页，硬件不会用它。

### 切换

- **用 `ljmp`，不要用 `lcall`**。`lcall` 会设置嵌套任务标志（NT），之后的 `iret` 会被当作"返回上一个任务"，逻辑会乱。
- 初始化时 `ltr` 加载任务 0 的选择子。**当前正在执行的代码流（monitor）就成了任务 0**。
- 新任务的 TSS 初值：`eip` = 一个启动桩，`esp` = 栈顶，`eflags` = 0x202（IF=1），段寄存器全部是平坦选择子。
- 启动桩：调任务入口，入口返回后调 `rm_task_exit()`。

### 协作式

`rm_yield()`：找下一个 READY 任务，`ljmp` 过去。没有别的任务就直接返回。

### 抢占式

1. PIC 重映射：主片 → 0x20–0x27，从片 → 0x28–0x2F（避开 CPU 异常 0–31）。
2. PIT 通道 0，100Hz：`0x43 ← 0x36`，`0x40 ← 11932 的低字节，再写高字节`（1193182 / 100）。
3. IRQ0 处理：`tick++` → **先发 EOI** → 调度 → `ljmp`。
   必须先发 EOI：否则 PIC 会一直等这个 EOI，直到切走的任务某天切回来为止，其间时钟中断全都丢了。
4. 被切走的任务之后被切回来时，会从 IRQ0 处理函数里的 `ljmp` 之后继续执行，然后 `iret` 回到被打断的地方。

### FPU 惰性保存

- 硬件任务切换**每次都会置 CR0.TS**。之后第一条 x87 指令触发 #NM（向量 7）。
- #NM 处理：`clts`；如果 FPU 当前属于别的任务，就把状态 `fsave` 到那个任务的 `fpu[]`；
  然后对当前任务 `frstor`（第一次用 FPU 的任务改为 `fninit`）；记下 FPU 现在属于当前任务。
- KERN 层不碰浮点，所以 #NM 只会从 APP 代码触发。
- CR0 要求：EM = 0，MP = 1。

### 退出与回收

`rm_task_exit()` 不能释放自己正在用的栈。做法：标记为 DEAD，切走；由任务 0（或空闲循环）
负责调用 `rm_free_owner(id)` 回收内存，并释放 TSS 槽位。

### IDT 归 resman

- resman 建新的 256 项 IDT，接管 0–31 号异常和 0x20–0x2F 号 IRQ。
- 提供 `rm_set_vector(n, handler)`。monitor 用它挂单步（1）、断点（3）等调试异常。
  monitor 现有的 `isr.s` / `ctx` 机制尽量保留，只是由"自己 lidt"改成"向 resman 登记"。

## 里程碑

| # | 内容 | demo / 验收 |
|---|---|---|
| T0 | resman 接管 GDT（加 TSS 空位）和 IDT；monitor 改用 `rm_set_vector` | **行为不变**：monitor 的单步、断点都和以前一样；`gfx_enter`（跳板）仍然能用 |
| T1 | `ltr` + 创建任务 + 协作式 yield | 两个任务交替打印 A 和 B |
| T2 | PIC 重映射 + PIT | monitor `tk` 显示 tick 在增长，但还不调度 |
| T3 | 抢占 | 两个死循环任务各自的计数器都在增长 |
| T4 | FPU 惰性保存 | 两个任务同时做浮点累加，结果和单独运行时一样 |
| T5 | 退出与回收 | 任务退出后 `mm` 显示它的内存已释放 |
| T6 | ABI 加锁审计 | 按 [02](02-decisions.md) D5 列出的位置逐个加锁；两个任务同时疯狂 `puts`，屏幕不乱、不死机 |

## 待决

- CNC 场景以后可能需要"实时任务"（固定周期、高优先级，用来发步进脉冲）。第一版的调度是平等轮转；实时类以后单独设计，可能直接在 IRQ0 里做，不走任务切换。
- 时钟频率：100Hz 够用吗？步进控制可能要求更高，那部分建议用 PIT 的另一个通道或 APIC 定时器单独处理。
