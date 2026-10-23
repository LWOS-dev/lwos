# 30 — 网络：netdev 抽象、目录布局、传文件协议

## 目标

**不停机把文件从宿主机传到目标机**。具体做法是用一根网线直连，两端收发裸以太网帧，
**不实现 IP/TCP**。

最有价值的用法是：宿主机 `make` 完，把新的 EXE 直接推到目标机内存里运行，
不用重写磁盘、也不用重启（和 [40 EXE](40-lwp-exe.md) 配合）。

## 现状

已实现首版 IRQ 驱动的 LWFT 文件接收：`app/resman/src/net.c` 管理
82540EM（8086:100E）的 RX/TX DMA 环和 PIC/INTx，接收任务无数据时阻塞。
`external/coreutils/netrecv.c` 安装到 `/BIN/NETRECV.EXE`；宿主工具
`tools/lwcon/lwcon.c` 支持 `lwcon IFACE put FILE`，停等 ACK、重试和 CRC32。
共用线协议在 `lib/include/lwft.h`。monitor 的 `nr` 也改用 resman IRQ 接口。
`make test-netrecv` 已覆盖 QEMU PC/Q35 上的收发与落盘。
82567LM（8086:10F5）已加入保留 BIOS 配置的 D0/PHY 接管分支和 `netrecv -d`
诊断，已通过寄存器模型测试，尚待真机验证。尚未实现网络加载执行、完整 ICH
PHY/NVM 复位初始化、共享 IRQ 分发或 IP/TCP。
下面的 `lib/abi/net/` 拆分仍是后续设计；当前驱动实际放在 resman 内。
命令用法、覆盖和超时语义见 [coreutils 说明](../external/coreutils/README.md#netrecv通过裸以太网接收文件)。

## 依赖

- 前置：[20 PCI 枚举](20-pci-enum.md)、[50 resman 内存](50-resman-memory.md)（描述符环和缓冲，resman 之前可用静态 bss 顶替）。
- 后继：[40 EXE](40-lwp-exe.md)（从网络加载）、以后可能的 FS 写入（把收到的文件落盘）。

## 设计

### 目录布局

每个网卡型号一对 `.c/.h`，同一家族的共用寄存器定义单独放：

```
lib/abi/net/
  netdev.h            NETDEV 结构（对外），所有驱动都实现它
  e1000_regs.h        Intel e1000 家族共用的寄存器偏移和位定义
  e1000_ring.c/.h     家族共用：描述符环的建立、收、发（82540EM 和 82567LM 这部分一样）
  i82540em.c/.h       82540EM：模拟器（Bochs/QEMU 的 e1000）
  i82567lm.c/.h       82567LM：真机（8086:10F5）
  (以后) rtl8139.c/.h ...
```

`lib/abi/build.mk` 里 `ABI_SRCS` 要加上 `net/` 子目录的规则。
每个驱动在 ABI 内部的驱动表里登记一项（匹配 vendor:device），见 [02](02-decisions.md) D3。

### 为什么要拆出 e1000_ring

82540EM 和 82567LM 属于同一家族：**描述符格式、收发环寄存器、RCTL/TCTL 基本相同**。
差别主要在**复位、PHY、MAC 地址来源**这几处（见 [32](32-net-i82567lm.md)）。
把相同的部分放进 `e1000_ring.c`，型号文件只写各自的差异。这样模拟器上调通的收发代码，
在真机上可以原样复用。

### NETDEV

```c
typedef struct _NETDEV {
    DWORD version, size;
    char  name[8];                      /* "eth0" */
    BYTE  mac[6];
    WORD  mtu;                          /* 1500 */
    int  (*link_up)(struct _NETDEV *d);
    int  (*send)(struct _NETDEV *d, const PVOID frame, DWORD len);  /* 完整以太网帧，不含 FCS */
    int  (*recv)(struct _NETDEV *d, PVOID buf, DWORD max);          /* 无帧返回 0 */
    void (*poll)(struct _NETDEV *d);
    PVOID priv;
} NETDEV, *PNETDEV;
```

### 传文件协议 LWFT

- 以太网类型用 **0x88B5**（IEEE 保留给本地实验用的类型号），不会和正常网络流量冲突。
- **停等协议**：每发一个数据块就等对方 ACK，超时重发。直连网线几乎不丢包，这样最简单、不会错。

帧负载格式：

```
+0  DWORD magic   'LWFT'
+4  BYTE  op      1=HELLO 2=PUT 3=DATA 4=END 5=ACK 6=NAK 7=RUN
+5  BYTE  flags
+6  WORD  len     本帧 data 的字节数
+8  DWORD seq
+12 DWORD arg     PUT: 文件总长；END: CRC32；RUN: 入口参数
+16 data[len]     DATA: 最多 1024 字节；PUT: 文件名
```

流程：

```
宿主                         目标机
HELLO ───────────────────→
      ←─────────────────── ACK（带上目标机 MAC）
PUT(name, size) ─────────→   resman 分配 size 字节
      ←─────────────────── ACK
DATA(seq=0..n) ──────────→   逐块写入
      ←─────────────────── ACK(seq)
END(crc32) ──────────────→   校验
      ←─────────────────── ACK / NAK
RUN ─────────────────────→   把缓冲当 LWP 加载并运行（见 40）
```

### 宿主端工具

`tools/lwft/lwft.c`：Linux 的 `AF_PACKET` 原始套接字，需要 root 或 `CAP_NET_RAW`。

```
lwft <网卡名> put <文件>          传到目标机内存
lwft <网卡名> run <文件>          传完立刻运行
```

模拟器下的连接方式：
- **QEMU**：`-netdev tap,id=n0,ifname=tap0,script=no -device e1000,netdev=n0`，宿主端对 `tap0` 收发。
- **Bochs**：本机的 Bochs（3.0.devel，自己编译装在 `/usr/local`）**没有编进网卡**，`bochs --help features` 只列出 `pci sb16`（2026-10-01 查过）。
  要用就得重新 `./configure --enable-pci --enable-e1000 ...` 编译，然后在 bochsrc 里加
  `e1000: enabled=1, mac=52:54:00:12:34:56, ethmod=tuntap, ethdev=/dev/net/tun:tap0`〔ethdev 的写法需查证〕。
  注意 `ethmod` 选 `vnet` 或 `slirp` 不行：它们只转发 IP，自定义以太网类型 0x88B5 的帧到不了宿主。

## 里程碑

| # | 内容 | demo / 验收 |
|---|---|---|
| N1 | 82540EM 在 QEMU 下：复位、读 MAC、建环 | monitor 打印 MAC，和 QEMU 命令行里给的一致 |
| N2 | 发一帧 | 宿主 `tcpdump -i tap0 -e ether proto 0x88b5` 抓到 |
| N3 | 收一帧 | 宿主发，目标机打印收到的内容 |
| N4 | LWFT PUT | 传一个 100KB 的文件，CRC32 一致 |
| N5 | LWFT RUN | 传一个 LWP 程序并运行（依赖 40 的 X2） |
| N6 | 82567LM 真机 | 见 [32](32-net-i82567lm.md) |

## 待决

- 在真机上迭代 82567LM 驱动时，驱动本身还没通，没法用网络传新版本，只能重写磁盘再重启。这一段的迭代成本躲不掉，所以一定要**先在模拟器上把 e1000_ring 调透**。
- 以后要不要做 UDP/IP，让宿主不需要 root？可以先不做。
