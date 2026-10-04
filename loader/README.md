# 启动期 BIOS 读盘

loader 不再自行操作 IDE/ATA 端口。stage2 用 BIOS 加载 loader 后，留下一张跳板入口表；loader 仍通过 BIOS 读取 BOOT.INI、ABI.BIN 和 MONITOR.BIN。

按这个顺序阅读代码：

1. `loader/loader.c:disk_read_checked`：FAT 读取的统一入口。失败时打印 LBA、错误码并停止，避免使用旧缓冲区内容。
2. `loader/bootdisk.c:boot_disk_read`：构造一扇区的 DAP、调用跳板、检查 CF/AH，成功后将中转缓冲区复制到目标地址。失败最多重试三次。
3. `boot/stage2.s:tramp_disk_read`：保存调用方状态，选择磁盘请求，进入与视频跳板共用的 `tramp_enter`。
4. `boot/stage2.s:tramp_enter.disk`：切回实模式后恢复启动时的 PIC 屏蔽状态，设置 DS:SI、DL、AH，执行 INT 13h；立即保存 FLAGS，再关闭中断并返回保护模式。

地址约定写在 `lib/include/bootdisk.h`，对应 stage2 中的常量：

| 地址 | 内容 |
| --- | --- |
| `0x90EE` | BIOS 启动盘号，由 stage2 保存 |
| `0x90F0` | 16 字节磁盘地址包 DAP |
| `0xA018` | 返回 AX，高字节 AH 为 BIOS 状态 |
| `0xA026` | 返回 FLAGS，bit 0 为 CF |
| `0xA028` | 原视频跳板地址，保持不变 |
| `0xA02C` | 请求类型：0 视频，1 磁盘 |
| `0xA030` | 新磁盘跳板地址 |
| `0xA034` | 启动时 BIOS 的两个 PIC 屏蔽字节 |
| `0xD000..0xD1FF` | 保留的 512 字节中转缓冲区 |

一次读取的过程：

```text
boot_disk_read(lba, count, destination)
  → DAP.count=1, DAP.segment=0, DAP.offset=0xD000, DAP.lba=lba
  → 调用 0xA030 中保存的入口
  → 保护模式 → 实模式 → INT 13h/AH=42h
  → 保存 BIOS 返回状态 → 保护模式
  → CF=0 时复制 512 字节到 destination
  → 推进 LBA 和 destination，直到 count 个扇区全部读完
```

正常目的地在 64KB 以上；原有 BOOT.INI 缓冲区 `0x500..0x8FF` 是唯一低地址例外，大小限制为 1KB。这个例外不会覆盖 stage2（从 `0x900` 开始）。

磁盘跳板仅用于启动期：不支持分页、非平坦段或原生驱动接管后的调用，也不负责恢复被重新映射的 PIC。以后原生 AHCI 驱动接管磁盘后，应结束 BIOS 读盘。视频跳板原有参数和入口保持不变。

验证命令：

```sh
make
python3 tests/boot_test.py
```

测试以 QEMU 快照分别启动 PC/IDE 和 Q35/AHCI，检查进入 monitor，以及视频模式进入/退出；不会写入原镜像。
