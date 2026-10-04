# AHCI 常量框架阅读与亲手实现

本文由 AI 编写。重构前的文件原样保存在 `ahci_reference.c`。
正式文件仍是 `ahci.c`, 与这份文档放在同一目录, 便于对照。

当前 AHCI 只有 include 和寄存器常量, 没有 ahci_detect / ahci_rw 实现,
也没有进入 KERN_LIB_SRCS。它目前没有 puts 依赖。
本次只把端口偏移宏里的未定义类型 u32 改为项目的 DWORD。
不要把常量定义和语法检查通过理解为 AHCI 驱动已经可用。

## 第一段: include 与接口

`dev/ahci.h` 声明探测和读写接口, 复用 ATA_DEV 保存设备信息。
`pci.h` 供未来寻找控制器使用。
AHCI 通过内存映射寄存器和 DMA 传输, 与 legacy ATA 的 inb/outb PIO 路径不同。
现有声明是未来接口, 调用它们目前会因缺少定义而链接失败。

## 第二段: 控制器全局寄存器

下表偏移相对控制器的 MMIO 基址, 不是 I/O 端口号。

| 宏 | 偏移 | 作用 |
|---|---|---|
| AHCI_CAP | 0x00 | 控制器能力 |
| AHCI_GHC | 0x04 | 全局控制 |
| AHCI_IS | 0x08 | 全局中断状态 |
| AHCI_PI | 0x0c | 实现了哪些端口的位图 |
| AHCI_VS | 0x10 | AHCI 版本 |

AHCI_GHC_AE 是 GHC 的 bit31, 用于 AHCI enable。
未来要从 PCI 配置取得 ABAR, 确认 MMIO 可访问, 而不是直接使用裸偏移。
涉及设备交接、复位和 DMA 的完整流程仍未实现。

## 第三段: 端口寄存器

`AHCI_PORT_BASE(port)` 计算端口寄存器块偏移:

```text
port 0: 0x100
port 1: 0x180
port 2: 0x200
每个端口占 0x80 字节
```

端口寄存器的偏移要再加上这个块偏移。示例仅用于手算:

```text
假设 ABAR = 0xfebf0000
port = 2
PxCMD 地址 = 0xfebf0000 + 0x200 + 0x18 = 0xfebf0218
```

| 宏 | 用来做什么 |
|---|---|
| PxCLB / PxCLBU | 命令列表物理地址的低/高部分 |
| PxFB / PxFBU | 接收 FIS 缓冲区物理地址的低/高部分 |
| PxIS / PxIE | 端口中断状态 / 使能 |
| PxCMD | 命令与 FIS 引擎控制、运行状态 |
| PxTFD | ATA task file 状态 |
| PxSIG | 设备签名 |
| PxSSTS / PxSCTL | SATA 链路状态 / 控制 |
| PxSERR | SATA 错误状态 |
| PxCI | 已提交的命令槽位位图 |

实际 MMIO 访问需要正确的 volatile 语义、映射和访问顺序;
这里只计算地址, 不建议现在直接解引用示例地址。

## 第四段: PxCMD 位

ST 是启动命令引擎, FRE 是启动 FIS 接收引擎;
CR 和 FR 是相应引擎仍在运行的状态。
未来重新设置 DMA 缓冲区前必须按硬件要求停止引擎并等待状态清除。

可以先在纸上练习位操作:

```c
/* 普通整数例子, 不是 MMIO 初始化流程。 */
DWORD cmd = 0;
cmd |= AHCI_PxCMD_FRE; /* 0x10 */
cmd |= AHCI_PxCMD_ST;  /* 0x11 */
cmd &= ~AHCI_PxCMD_ST; /* 0x10 */
```

## 第五段: 签名、FIS 和命令

AHCI_SIG_ATAPI 是 ATAPI 签名; 看见它不代表已经有 ATAPI 读写支持。
AHCI_IS_TFES 是端口 task-file 错误位。
AHCI_FIS_REG_H2D 标识主机向设备发送的寄存器 FIS 类型。
READ_DMA_EXT / WRITE_DMA_EXT 是未来放进命令 FIS 的 ATA 操作码。

命令不能只写一个操作码就完成: 还要准备命令头、命令表、FIS、
PRDT (DMA 缓冲区描述表), 再提交槽位并检查完成情况。
这些结构和函数目前都没有。

## 第六段: 端口上限

AHCI_MAX_PORTS 当前等于 ATA_MAX_DEVICES, 即 4。
这是框架复用设备表的容量限制, 不代表硬件最多只有四个端口。
实现时需要区分硬件端口编号与已发现设备数组的索引,
并明确多控制器如何保存状态。

## 后续实现的职责和 puts 边界

可以先画出这条路径, 再每次完成其中一步:

```text
PCI 找控制器 → 取得 MMIO 基址 → 看 PI / 链路状态
→ 准备端口 DMA 内存 → IDENTIFY → 填 ATA_DEV
→ 准备读命令 → 提交 → 等待完成 → 返回状态和 buffer
```

所有探测结果和错误都交给调用方显示。ahci_detect 返回数量的现有接口
不能表达完整探测错误, 后续可以由你决定增加结果结构或错误查询接口。
不需要为了调试把 text.h 或 puts 引入硬件驱动。

DMA 使用物理地址; 不能把任意指针直接当作设备可访问的地址。
现阶段身份映射也需验证地址范围、对齐、缓冲区寿命和控制器寻址能力。
命令列表、接收 FIS、命令表分别有硬件对齐要求, 实现前应核对官方 AHCI 规范。
本文是现有代码导读, 不是完整硬件初始化规范。

## 建议的亲手实现顺序

1. 先只列出 PCI 控制器和 ABAR, 输出放 monitor。
2. 只读能力、端口位图和签名, 不提交 DMA。
3. 定义并核对命令相关结构和内存布局。
4. 实现 IDENTIFY, 复用 ata_identify_parse 解析结果。
5. 只读一个已知扇区, 比较内容, 再实现多扇区。
6. 最后接入块设备接口, 然后才考虑写入与 flush。

当前正式 ATA 的 AHCI backend 分支仍是拒绝服务的占位。
仅仅写完 ahci_rw 还不够, 必须明确连接到 ATA 或直接注册 AHCI BLKDEV。
构建中保持显式源码列表, 不让 *_reference.c 被通配符误编译。
