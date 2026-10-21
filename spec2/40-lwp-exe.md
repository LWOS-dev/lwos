# 40 — LWP 可执行格式与重定位加载

## 目标

- 定义 LWP 格式：EXE 和 DLL 共用一种头。
- 宿主端工具 `mkexe`：ELF → LWP。
- resman 里的加载器：把 LWP 放到**任意地址**，打重定位，跑起来。

## 现状

`tools/mkexe/doc.txt` 只有头部字段草稿，没有代码。草稿里有两处注释对不上：

| 偏移 | 草稿字段 | 草稿注释 | 问题 |
|---|---|---|---|
| +0x1C | memory size | (file size) | "内存大小"却注释成文件大小 |
| +0x20 | stack size | (image + bss) | "栈大小"却注释成映像+bss |

看起来是注释错位了一行：`(file size)` 应该属于 image size，`(image + bss)` 应该属于 memory size。
下面的 v1 头按这个理解整理。

## 依赖

- 前置：[50 resman 内存](50-resman-memory.md)（分配映像内存）。
- 文件来源（任选其一即可开工）：LOADER.BIN 预先放进内存（见 X2）、[22 FS](22-blockdev-fs.md)、[30 网络](30-net.md)。
- 后继：[41 DLL](41-dll.md)、[51 任务](51-tasks.md)（EXE 作为任务运行）。

## 设计

### 段模型的后果

按 [02](02-decisions.md) D1 全平坦，每个 EXE 被加载到哪里事先不知道，所以：
**一律链接到地址 0，加载时对每个绝对地址加上加载基址。**

### 头（v1）

```
偏移  类型    字段          说明
+00   DWORD  magic         'LWP\0' = 0x0050574C
+04   WORD   version       1
+06   WORD   hdr_size      头的字节数，以后加字段就变大
+08   DWORD  flags         bit0 DLL；bit1 用了 FPU（APP 层编译）；bit2 无重定位（固定地址）；bit3 NWIN；bit4 TEXT；bit5 GFX
+0C   DWORD  abi_need      需要的 ABI 最小 slot 数（见下）
+10   DWORD  entry         入口 RVA（相对映像开头）
+14   DWORD  image_off     映像在文件里的偏移
+18   DWORD  image_size    映像在文件里的字节数（text+rodata+data）
+1C   DWORD  mem_size      映像在内存里的字节数（= image_size + bss），≥ image_size
+20   DWORD  stack_size    EXE 的栈大小；DLL 填 0（用调用者的栈）
+24   DWORD  reloc_off     重定位表在文件里的偏移
+28   DWORD  reloc_count   重定位项数
+2C   DWORD  sym_off       符号表偏移（给 monitor 反汇编/回溯用，可为 0）
+30   DWORD  sym_count
+34   DWORD  export_off    导出表（DLL 用，见 41）
+38   DWORD  export_count
+3C   DWORD  crc32         映像部分的 CRC32，0 = 不校验
```

**`abi_need` 怎么定**：ABI 的 slot 只追加，所以"ABI 有多少个 slot"本身就是版本号。
建议在 ABI 表里加一个 slot 返回 `LW_SLOT_COUNT`。mkexe 把编译时的 `LW_SLOT_COUNT` 写进 `abi_need`，
加载器发现运行中的 ABI 比它小就拒绝加载。resman 表同理（以后头里再加一个 `rm_need`）。

TEXT/NWIN/GFX 的互斥规则、文本资源准备和普通 main 的 CRT 启动链见
[61 文本窗口与标准输出](61-text-console-stdio.md)。TEXT 使用现有 `LWP_FLAG_TEXT = 0x10`。

### 重定位表

每项一个 DWORD：**需要修正的那个 32 位字的 RVA**。加载时对每一项做 `*(DWORD*)(base + rva) += base`。

### mkexe（宿主端，`tools/mkexe/`）

1. 应用程序用 `tools/mkexe/lwp.ld` 链接：从 0 开始，text → rodata → data → bss 依次排列；
   **链接时加 `-q`（`--emit-relocs`）**，让最终的 ELF 保留重定位节。
2. mkexe 读 ELF：
   - 把 PT_LOAD 段拼成映像；
   - 遍历 `.rel.*` 节：`R_386_32` → 记入重定位表；`R_386_PC32` 是映像内部的相对跳转，跳过；
   - **有未定义符号就报错**。调用 ABI、resman、DLL 都要通过函数表，不允许直接链接外部符号；
   - 其他重定位类型一律报错（说明编译选项不对，例如开了 PIC）。
3. 写出 LWP 文件。另外提供 `mkexe -d x.lwp`，把头和重定位表打印出来，调试用。

编译选项必须保持 `-fno-pie`，并且**不能**用 `-fPIC`。

### 入口约定

```c
typedef struct {
    DWORD  version, size;
    PVOID *abi;          /* ABI 表 */
    PVOID *rm;           /* resman 表 */
    int    argc;
    char **argv;
    LW_VI  vi;           /* 可视资源，定义见 lib/include/resman.h */
} LW_ENV;

int lwp_main(const LW_ENV *env);
```

应用链接一个很小的 `crt0.c`：把 `env->abi`、`env->rm` 存进全局变量，然后调 `main`。
加载器在调用入口前已经清好 bss、切好栈。

### 加载步骤（resman）

1. 校验 magic、version、`abi_need`、crc32。
2. `rm_alloc(mem_size, 16, 所有者)`。
3. 拷贝映像，把 `[image_size, mem_size)` 清零（bss）。
4. 打重定位。
5. 分配栈（`stack_size`）。对于 TEXT 程序，在入口执行前准备文本对象、窗口记录及 `env->vi`；
   失败须完整回滚，生命周期见 [61](61-text-console-stdio.md)。
6. 单任务阶段：切到新栈，直接 `call` 入口；返回后切回，释放内存。
   任务阶段：创建任务（见 [51](51-tasks.md)）。

## 里程碑

| # | 内容 | demo / 验收 |
|---|---|---|
| X0 | 把 `doc.txt` 换成上面的 v1 头 | 文档定稿 |
| X1 | mkexe：ELF → LWP，`-d` 打印 | 一个 hello 程序转出来，`-d` 显示重定位项数 > 0（字符串指针会产生重定位） |
| X2 | 加载器，从内存加载 | BOOT.INI 加一行 `LOAD "HELLO.LWP" TO 400000H`，把**原始文件**放进内存；monitor 加命令 `x 400000`，把它加载到 resman 分配的地址上运行，打印 hello |
| X3 | 重定位正确性 | 把同一个 LWP 连续加载两次（两次地址不同），两次都正确打印 |
| X4 | 从文件系统加载 | 依赖 22 的 B3：`run /HELLO.LWP` |
| X5 | 从网络加载 | 依赖 30 的 N5：`lwft run hello.lwp` |

X2 的好处是**不需要 FS，也不需要网络**：LOADER.BIN 现有的 LOAD 命令就能把文件放进内存。

## 待决

- 符号表格式：建议 `{DWORD rva; DWORD name_off}` + 字符串区，按 rva 排序，方便 monitor 做地址 → 函数名。
- 要不要支持命令行参数？`argc/argv` 先留着，第一版传 0。
