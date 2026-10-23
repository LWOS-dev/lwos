# LWP 手写汇编与二进制速查表（无需宿主机 / mkexe）

本文面向直接在 LWOS 上写程序的人：自己组织 **LWP 文件头、机器码、数据和重定位表**，
最终保存为 `HELLO.EXE`，交给加载器运行。无需 ELF、链接器、CRT 或 mkexe。

下面用 NASM 风格描述 32 位 x86 指令和字节布局，**不假定 LWOS 已安装 NASM**。
有能输出原始二进制的汇编器时可按其语法改写；只有能按字节写文件的编辑工具时，
可以直接使用第 3 节的完整十六进制内容。纯文本汇编源码不会被加载器直接执行。

## 1. 最小 hello 要有哪些部分

```text
文件偏移 0x00：LWP 文件头，固定 0x40 字节
文件偏移 0x40：映像开始 = RVA 0；先放入口代码，再放字符串
映像末尾：     重定位表，每项一个 32 位 RVA
```

| 部分 | 这个 hello 是否需要 | 作用 |
|---|---|---|
| LWP 文件头 | 必需 | 告诉加载器入口、映像大小、栈大小、重定位表位置 |
| 代码 | 必需 | 取 env、查 ABI 表、调用输出、返回 |
| 字符串 / 常量 | 必需 | `Hello, world!` 和结尾 NUL |
| 有初值的可写变量 | 不需要 | 用到时放在映像内 |
| BSS / 零初始化缓冲区 | 不需要 | 用到时增加 mem_size，由加载器清零 |
| 重定位表 | 本例需要一项 | 修正 push 指令里的字符串地址 |
| 栈内容 | 不写进文件 | 加载器按 stack_size 另外分配 |

LWP v1 **没有节头表**，不要求声明 `.text/.rodata/.data/.bss` 这些段名。
对手写文件而言，关键是字节顺序、大小和偏移正确。代码与数据的分区由你自己安排。

## 2. 完整 hello：头 + 映像 + 重定位表

以下为完整源文件，没有省略的文件头或打包步骤。所有标签按文件起点 0 计算：
**写文件偏移用 `label - file_start`，写映像 RVA 用 `label - image`。**

`db/dw/dd` 分别写 1/2/4 字节；多字节数按小端写出。
`ORG 0` 只是标签计数起点，不代表程序会加载到物理地址 0。

```nasm
BITS 32
ORG 0
file_start:
    dd 0x0050574c                    ; magic：LWP + NUL
    dw 1, 0x40                       ; version、hdr_size
    dd 0
    dd 0x13
    dd lwp_main - image              ; 入口 RVA
    dd image - file_start            ; 映像文件偏移
    dd image_end - image
    dd image_end - image
    dd 0x10000
    dd relocs - file_start           ; 重定位表文件偏移
    dd (relocs_end - relocs) / 4      ; 重定位项数
    dd 0, 0
    dd 0, 0
    dd 0
image:
lwp_main:
    push ebp
    mov ebp, esp
    and esp, -16
    mov eax, [ebp + 8]               ; env 指针
    test eax, eax
    jz bad_env
    cmp dword [eax], 1
    jne bad_env
    cmp dword [eax + 4], 44
    jb bad_env
    mov edx, [eax + 8]               ; env->abi
    test edx, edx
    jz bad_env
    cmp dword [edx], 0x4241574c
    jne bad_abi
    mov eax, [edx + 0x12 * 4]        ; ABI 输出函数指针
    test eax, eax
    jz bad_abi
    sub esp, 12                      ; 加上参数后栈按 16 字节对齐
    db 0x68                          ; push imm32 的操作码
arg_message:
    dd message - image               ; 其 4 字节立即数：字符串 RVA
    call eax
    add esp, 16
    xor eax, eax
    jmp done
bad_env:
    mov eax, -1
    jmp done
bad_abi:
    mov eax, -5
done:
    mov esp, ebp
    pop ebp
    ret
message: db 'Hello, world!', 10, 13, 0
image_end:
relocs:
    dd arg_message - image           ; 指向要修正的立即数字段
relocs_end:
```

头部对应字段从上到下是：magic，version/hdr_size，flags，abi_need，entry，
image_off，image_size，mem_size，stack_size，reloc_off，reloc_count，
sym_off/sym_count，export_off/export_count，crc32。

本例 `abi_need=0x13`：用到编号 `0x12` 的槽，函数表至少需要 19 个槽。
`flags=0` 使用传统 ABI 控制台；不要改成 TEXT 标志来期待自动得到文本窗口。

特意用 `db 0x68` + `dd message - image` 写出 `push imm32`：
这样标签 `arg_message` 精确指向要修正的 **4 字节立即数字段**，也避免汇编器将
push 优化成短立即数形式。不能把这个字段缩成 1 字节。

加载器已经进入 32 位保护模式并设置栈。入口是普通 cdecl 函数，`ret` 回到调用者；
不需要初始化段寄存器，不要用 DOS 中断、Linux 系统调用或 `hlt` 退出。

## 3. 没有汇编器：直接输入这些字节

这是上面程序生成的完整文件，共 **0xA3 = 163 字节**。
左侧是文件偏移，冒号和偏移数字不属于文件，只写右侧字节。
必须按二进制保存，不能把这段十六进制当普通文本保存成 `.EXE`。

```text
0000: 4C 57 50 00 01 00 40 00 00 00 00 00 13 00 00 00
0010: 00 00 00 00 40 00 00 00 5F 00 00 00 5F 00 00 00
0020: 00 00 01 00 9F 00 00 00 01 00 00 00 00 00 00 00
0030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0040: 55 89 E5 83 E4 F0 8B 45 08 85 C0 74 32 83 38 01
0050: 75 2D 83 78 04 2C 72 27 8B 50 08 85 D2 74 20 81
0060: 3A 4C 57 41 42 75 1F 8B 42 48 85 C0 74 18 83 EC
0070: 0C 68 4F 00 00 00 FF D0 83 C4 10 31 C0 EB 0C B8
0080: FF FF FF FF EB 05 B8 FB FF FF FF 89 EC 5D C3 48
0090: 65 6C 6C 6F 2C 20 77 6F 72 6C 64 21 0A 0D 00 32
00A0: 00 00 00
```

文件可命名为 `HELLO.EXE`。在 monitor 文件列表选中后按 F4 前台运行，
输出到 ABI 控制台并返回。它不是 Windows PE 文件。

这个固定样本的关键位置如下；代码或字符串长度改变后，需要重新计算，不能沿用旧偏移。

| 内容 | 文件偏移 / 值 |
|---|---|
| 文件头 | `0x00 .. 0x3F` |
| 入口 | 文件 `0x40`，RVA `0` |
| 映像 | 文件 `0x40 .. 0x9E`，共 `0x5F` 字节 |
| push 操作码 | 文件 `0x71`，字节 `68` |
| 要修正的立即数字段 | 文件 `0x72 .. 0x75`，RVA `0x32`，原值 `0x4F` |
| 字符串 | 文件 `0x8F .. 0x9E`，RVA `0x4F`，含 LF/CR/NUL |
| 重定位表 | 文件 `0x9F .. 0xA2`，唯一项 `32 00 00 00` |
| 栈大小 | `0x10000`，64 KiB，由加载器分配 |

修改字符串但保持总长度不变时，保留尾部 `00` 即可。
增加字符串长度、插入指令或改变指令编码时，需要更新受影响的大小、偏移、跳转和地址值；
汇编器可通过标签表达式计算，手输机器码时由自己计算。

## 4. 三种坐标：文件偏移、RVA、运行地址

```text
文件偏移 = image_off + RVA       （只适用于文件里的映像字节）
运行地址 = base + RVA            （base 是加载器实际分配的映像地址）
入口地址 = base + entry
```

文件头和重定位表不属于映像，不会作为程序代码复制到 `base`。
所以不能把文件中的标签值直接当映像地址。例如字符串文件偏移为 `0x8F`，
它的 RVA 是 `0x8F - 0x40 = 0x4F`，push 字段必须先写 `0x4F`。

加载到 `base=0x500000` 时，加载器处理表项 `0x32`：

```text
去修改：base + 0x32 = 0x500032 处的 DWORD
原值：  0x0000004F
新值：  0x0050004F，即字符串的实际地址
```

重定位表记的是**哪里要改**，不是字符串在哪里。
每项满足 `rva + 4 <= image_size`，不得重复或相互重叠，可以不对齐。

| 手写内容 | 是否加入重定位表 |
|---|---|
| 指令中的映像地址立即数 | 是；初值写目标 RVA，表项写立即数字段 RVA |
| 数据中的指针 DWORD | 是；初值写目标 RVA，表项写该 DWORD 的 RVA |
| 指令中的绝对内存地址，如 `[counter]` | 是；修正指令里的 32 位地址字段 |
| 映像内部的相对 call / jmp 位移 | 否；整体搬移不会改变距离 |
| 从 env 取得的 ABI 指针、函数地址 | 否；已经是运行时地址 |
| 普通数值、ABI 槽位偏移、文件头里的 RVA | 否 |

要使用 NASM 风格绝对寻址时，应写类似 `[counter - image]`，并将编码内的
地址字段加入重定位表。单写 `[counter]` 在本例 ORG 0 下会把文件头长度也算进去。
没有 ELF 转换器时，汇编器不会自动替你整理这张 LWP 表。

## 5. 文件头字段速查

全部小端；WORD=2 字节，DWORD=4 字节。下表偏移为十六进制。

| 偏移 | 宽度 | 字段 | 如何填写 |
|---|---|---|---|
| `00` | DWORD | magic | `0x0050574C`，磁盘字节 `4C 57 50 00` |
| `04` | WORD | version | 1 |
| `06` | WORD | hdr_size | `0x40` |
| `08` | DWORD | flags | 本例 0 |
| `0C` | DWORD | abi_need | 所需 ABI 最小槽数，本例 `0x13` |
| `10` | DWORD | entry | 入口 RVA，本例 0 |
| `14` | DWORD | image_off | 映像文件偏移，本例 `0x40` |
| `18` | DWORD | image_size | 文件中的映像长度，本例 `0x5F` |
| `1C` | DWORD | mem_size | 映像内存长度，含 BSS，不含栈；本例 `0x5F` |
| `20` | DWORD | stack_size | 独立栈字节数，本例 `0x10000` |
| `24` | DWORD | reloc_off | 重定位表文件偏移，本例 `0x9F` |
| `28` | DWORD | reloc_count | 项数，本例 1；无表时填 0，reloc_off 也填 0 |
| `2C / 30` | DWORD × 2 | sym_off / sym_count | 本例均填 0 |
| `34 / 38` | DWORD × 2 | export_off / export_count | 普通 EXE 均填 0 |
| `3C` | DWORD | crc32 | 填 0 表示不校验 |

重定位表在映像后面，**不计入 image_size 或 mem_size**。
如果要增加 256 字节零缓冲区，可以令 `mem_size = image_size + 256`，
缓冲区起始 RVA 为 `image_size`，文件里不用补这 256 个零；需要对齐时把填充也计入。
这样缓冲区的 RVA 可能等于重定位表相对于 image 的偏移，但两者分别属于内存尾部和文件元数据，
加载器不会把重定位表复制进缓冲区。

## 6. 入口与 ABI 调用速查

入口等价于 `int lwp_main(const LW_ENV *env)`：

```text
刚进入： [esp] = 返回地址，[esp+4] = env
建立帧： push ebp / mov ebp, esp 后，[ebp+8] = env
```

`LW_ENV` 当前 32 位布局，偏移为十六进制：

| env 偏移 | 字段 |
|---|---|
| `+00` | version，目前为 1 |
| `+04` | size，结构体实际字节数 |
| `+08` | abi，ABI 函数表指针 |
| `+0C` | rm，resman 函数表指针 |
| `+10 / +14` | argc / argv |
| `+18` | vi，20 字节可视资源描述 |
| `+2C` | cwd，可选的 256 字节工作目录尾部，读取前检查 size |

本例只读取基本字段，要求 `size >= 44`。输出函数 `lw_puts` 的槽号为 `0x12`，
每槽 4 字节，所以函数指针在 `[abi + 0x48]`，调用参数是以 0 结尾的字符串地址。
ABI 首 DWORD 为 `0x4241574C`，不是函数指针。不要重新调用系统 ABI 初始化入口。

| 调用约定 | 要点 |
|---|---|
| 返回值 | eax 放退出码；正常 0，然后 ret |
| 参数 | 从右往左压栈，调用方收回；本例是一个字符串指针 |
| 可被调用破坏 | eax / ecx / edx；跨调用需要的值自己保存 |
| 必须保留 | ebx / esi / edi / ebp，返回时恢复 esp |
| 栈对齐 | 调用 C ABI 函数前 esp 按 16 字节对齐；本例用填充补齐 |
| 方向标志 | 调用和返回时 DF 清零；用了 std 后要 cld |

格式与接口以 [lwp.h](../lib/include/lwp.h)、[abi.h](../lib/include/abi.h)
和 [resman.h](../lib/include/resman.h) 为准。本例直接向传统控制台输出；
GHOST 文本窗口需要额外的 TEXT 资源与初始化，另见
[文本窗口约定](../spec2/61-text-console-stdio.md)。
