# 50 — resman：内存资源管理

## 目标

resman 是 v2 的内核层（[02](02-decisions.md) D2）。这一篇讲它的骨架和内存管理；
任务管理见 [51](51-tasks.md)。

"资源管理"首先管的是**内存**，其次是**谁拥有哪个设备、哪个 IRQ**。
每一份资源都记录**所有者**，这样任务退出时可以一次性全部回收。

## 现状

`app/resman/` 已有预加载映像头、版本接口及 monitor 的调用/返回路径。
初始化复用旧页分配器，复制 E820 并保留低 4 MiB；尚未完成公开 alloc/free、
owner/tag 和按任务回收。monitor 也使用同一组固定地址分配元数据，所有权尚未统一。
当前启动方式是 monitor 的 `rm` 命令，下面的 ATTACH RESMAN 路线仍是目标方案。

## 依赖

- 前置：无（只需要 ABI 和 stage2 留下的 E820 表）。**这是整条路线里最该先做的模块。**
- 后继：几乎所有模块（DMA 缓冲、EXE 映像、任务栈、GUI 后备缓冲）。

## 设计

### 映像

```
lib/resman/               （从 app/resman 挪过来，用 CFLAGS_KERN）
  build.mk
  resman.ld               链接到 0x180000，开头是 .rmtab 节
  include/                私有头
  src/rm.c                表、初始化
  src/mem.c               分配器
lib/include/resman.h      公开头：LW_RM_* slot 号 + rm_* 宏（仿照 abi.h）
```

- 表结构和 ABI 同构（slot 0x00–0x04 是映像头）。
- BOOT.INI 顺序：LOAD ABI → LOAD RESMAN → LOAD MONITOR → ATTACH ABI → ATTACH RESMAN → PORT MONITOR。
- monitor 里仿照 `lw_abi_base` 定义 `rm_base`，用 `rm_attach()` 校验。

**loader 现在的 `A`/`P` 命令会挡住这件事**（已看过 `loader/loader.c:execute`、`head_check`）：

1. `head_check` 要求 slot 0 必须等于 `LW_ABI_MAGIC`。按 `abi.h` 开头的注释，slot 0x00–0x0F
   本来就是"所有可加载映像的通用头"（MONITOR.BIN 也用 LWAB），所以 **LWAB 的真实含义是
   "可加载映像"，而不是"这是 ABI"**。resman 的 slot 0 也填 LWAB，另外用一个 slot
   （例如 0x05 `LW_SLOT_IDENT`）放 `'LWRM'` 来区分身份。
2. `A` 命令成功后会执行 `lw_abi_base = h`。第二次 attach resman 时，这个变量会被覆盖成 resman 的表，
   接着 `P` 命令会把**resman 的表当作 ABI 表**压栈传给 monitor。需要改 loader：
   `A` 只在 IDENT 为 ABI 时才更新 `lw_abi_base`，或者干脆 `P` 直接传 `LW_ABI_BASE` 常量。
3. resman 的初始化函数没有参数，拿不到 ABI 表。直接在初始化函数里 `lw_abi_attach((PVOID)LW_ABI_BASE)` 即可，
   ABI 的地址本来就是固定的。

### 初始化时必须先做的事

**把 0x8000 的 E820 表拷进 resman 自己的 bss**。之后低端内存就可以复用了。

### 分配器

- 可分配区域 = E820 类型 1（可用）的区域 ∩ [0x300000, 4GB)，减去已知的固定映像。
- **元数据放在分配区之外**：一张静态的块表（例如 512 项），不在每块内存前面放头。
  原因是没有内存保护，APP 写越界会踩坏"块头"，而块表放在 resman 的 bss 里要安全得多。
- 块表项：

```c
typedef struct {
    DWORD base, size;
    WORD  owner;      /* 0 = 系统，其余为任务号 */
    WORD  flags;      /* FREE / USED / DMA */
    DWORD tag;        /* 4 字符，例如 'AHCI' 'STK ' 'IMG ' */
} RM_BLOCK;
```

- 策略：首次适应（first-fit），支持对齐（把前面的零头切成一个空闲块）；释放时和相邻空闲块合并。
- 所有者：任务阶段之前一律是 0。

### 接口（resman 表 0x10 段）

```c
PVOID rm_alloc(DWORD size, DWORD align, DWORD tag);
void  rm_free(PVOID p);
void  rm_free_owner(WORD owner);          /* 任务退出时调用 */
DWORD rm_mem_free(void);                  /* 剩余总量 */
void  rm_mem_dump(void);                  /* monitor 的 mm 命令 */
```

### 设备与 IRQ 所有权

设备表（resman 表 0x50 段）：resman 启动时用 [20](20-pci-enum.md) 扫 PCI，对每个功能调
`lw_drv_bind`（[02](02-decisions.md) D3），为驱动分配 DMA 内存（tag 为驱动名）。
得到的设备对象按类别登记：`rm_get_blk(i)`、`rm_get_net(i)`。

IRQ 所有权到任务阶段再做。

## 里程碑

| # | 内容 | demo / 验收 |
|---|---|---|
| R0 | RESMAN.BIN 骨架：表 + 初始化，由 BOOT.INI 加载 | monitor 调 `rm_*` 的版本 slot，打印 'LWRM' 和 slot 数 |
| R1 | 拷贝 E820，建立空闲区 | monitor `mm`：打印 E820 原表，以及扣掉固定映像后的空闲区。Bochs 32MB 下应剩约 29MB |
| R2 | alloc/free + 对齐 + 合并 | monitor 的测试命令：分配 1KB 对齐 1KB、256B 对齐 256B、5MB；打印地址（检查对齐）；全部释放后 `mm` 回到 R1 的状态 |
| R3 | tag / owner / `rm_free_owner` | `mm` 里每块显示 tag；按 owner 释放后块消失 |
| R4 | 设备表 | resman 启动时扫 PCI + 绑定驱动；monitor `dv` 列出设备对象 |

## 待决

- 块表 512 项够不够？每个任务至少占 2 块（映像 + 栈），驱动每个占 1–2 块，GUI 每个窗口 1 块，暂时够。
- 要不要做"小块分配"（几十字节的对象）？第一版不做，最小分配粒度 16 字节。
