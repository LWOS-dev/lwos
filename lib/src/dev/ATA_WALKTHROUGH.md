# ATA PIO 阅读与亲手重构

本文由 AI 编写。重构前的源码原样保存在 `ata_reference.c`,
正式源码仍是 `ata.c`; Makefile 显式选择正式文件, 不编译参考副本。
文档放在源码旁, 便于看函数时打开对照; 不进入程序映像。

ATA 正在供 ABI 的磁盘和块设备服务使用, 因此没有把它换成空入口。
本次只移除输出依赖并合并探测流程, 后续可以一段一段亲手重写。

## 代码的整体路径

```text
ata_detect → 初始化 → 探测四个位置 → IDENTIFY → devs[]
                                                     ↓
                         ata_count / ata_get → 调用方显示
                                                     ↓
ata_register_blockdevs → BLKDEV 回调 → ata_read / ata_write
                                      ↓
                                    xfer → PIO 寄存器读写
```

`devs` 保存至多四个设备, `n_devs` 只统计识别成功的设备。
`ATA_DEV` 包含通道、主从、容量、型号、LBA48 能力。
头文件里的 backend 是预留区分, 不表示当前已经支持 AHCI。

## 第一段: 错误码和寄存器地址

`ata_strerror` 把错误码转换为常量字符串, 不负责打印。
`base_of` / `ctrl_of` 根据通道返回端口地址:

| 通道 | 命令端口基址 | 控制端口 |
|---|---|---|
| primary (0) | 0x1f0 | 0x3f6 |
| secondary (1) | 0x170 | 0x376 |

例如 primary 的状态寄存器是 `0x1f0 + ATA_REG_STATUS = 0x1f7`。
这些是当前 legacy 固定端口方案; 尚未处理 PCI native-mode 的端口分配。

## 第二段: delay / wait / select

`ata_delay400` 连读四次控制端口, 用于设备选择后的短等待。
`ata_select` 写主从选择位, 再等待。
`ata_wait(channel, data)` 分两步:

1. 等 BSY 清除; 轮询次数用尽返回 TIMEOUT。
2. 若 data 非零, 继续检查错误并等 DRQ, 表示可以传输数据。

例子: 状态 0x80 表示 BSY; 0x08 表示 DRQ; 0x01 表示 ERR。
轮询次数限制不是精确的毫秒计时, 机器速度会影响实际时间。
当前 data=0 路径只等 BSY, 不完整检查命令完成错误;
以后可亲手把“等待空闲”和“检查命令完成状态”拆开。

## 第三段: IDENTIFY 字符串和容量

`copy_string` 从 WORD 数组复制型号等字段, 每个字先高字节再低字节,
最后去掉尾部空格, 补 NUL。

```text
id 中的字: 0x4142 0x4320
字符串:   A B C 空格
去尾空格: "ABC"
```

`ata_identify_parse` 读取序列号、固件号、型号和容量。
容量用 QWORD 保存; 512 字节扇区下, 2048 个扇区等于 1 MiB。
ATAPI 的容量不能从这里确定, 当前置为 0。

`ata_identify` 选择设备、清寄存器、发 IDENTIFY、等待并读取 256 个 WORD。
检测到 ATAPI 签名时改发 IDENTIFY PACKET。
识别成功不代表当前支持该设备的数据读写: xfer 会拒绝 ATAPI。

## 第四段: init / detect / count / get

`ata_init` 清设备表并关闭 legacy ATA 中断, 当前通过轮询读写。
`ata_detect` 遍历两个通道、每个通道的主从两个位置。
探测过程包括寄存器回读和 IDENTIFY, 失败不加入设备表。
`ata_count` 返回数量, `ata_get` 返回表内只读对象或空指针。

参考文件有一套打印探测、一套安静探测, 重复大部分逻辑。
正式文件现在只保留安静的实际探测, `ata_detect_quiet` 调用 `ata_detect`。
探测仍是 void 接口: 失败设备被跳过, 尚未提供逐位置的错误记录。
后续若需要诊断, 可以设计保存 channel、drive、error 的结果对象,
由调用方决定如何显示; 不必把 puts 放回驱动。

## 第五段: setup_lba28 / setup_lba48

两者只填任务寄存器, 真正的读写命令由 xfer 发出。
LBA28 把地址低 24 位放到三个 LBA 寄存器, 高 4 位放到设备选择寄存器。

```text
lba = 0x01234567
LBA0 = 0x67, LBA1 = 0x45, LBA2 = 0x23
设备选择寄存器里的地址高位 = 0x1
```

LBA48 使用高低两轮写寄存器, 高轮必须先写。
协议里 LBA28 计数 0 表示 256 扇区; LBA48 计数 0 表示 65536 扇区。
API 层的 sectors=0 却被当作无效参数, 编码为 0 是内部处理。

## 第六段: xfer / read / write

`xfer` 验证参数、决定 LBA28 或 LBA48、发命令,
每次等待一个扇区可传输, 再读写 256 个 WORD。

```text
读取 2 扇区 → 需要 1024 字节 buffer
第 0 扇区 → buffer[0..511]
第 1 扇区 → buffer[512..1023]
```

写入后还发送 flush 命令。read/write 是包装函数, 共享该流程。
当前 AHCI backend 返回 E_ATAPI, 只是未接入的占位, 不要理解为 AHCI 等于 ATAPI。
直接 ATA 接口的容量边界、48 位地址上限及加法溢出检查尚需补齐;
不要把“块设备适配层检查过”当作所有调用路径都检查过。

## 第七段: BLKDEV 适配层

`PIO_BLK_CTX` 保存 ATA 索引, BLKDEV 的 priv 指向它。
`pio_blk_xfer` 验证容量范围并分批, 每批最多 256 扇区。
例如读取 300 扇区, 拆成 256 + 44。
回调返回 0 或底层错误码; 它们也不输出。

`ata_register_blockdevs` 仅注册有容量的 ATA 磁盘,
设备和上下文用静态数组保存, 生命周期属于 ABI。
重新探测或重新注册需安排好顺序, 不能在使用旧块设备时修改索引映射。

## 解除 puts 的边界

正式 ATA 代码不再包含 `text.h`, 不调用 puts/putc/put_*。
硬件库编译也去掉了 ABI 私有头文件搜索路径。
调用方依然可以打印, 例如 monitor 通过 ABI 显示结果:

```c
/* 示例片段, 不是自动加入项目的实现。 */
lw_disk_probe();
for (BYTE i = 0; i < lw_disk_count(); ++i) {
    PCATA_DEV d = lw_disk_info(i);
    if (d) {
        lw_puts(d->model);
        lw_puts("\n\r");
    }
}
```

这样屏幕、网络日志或未来 GUI 使用同一份设备结果。
`ata_strerror` 返回文本仍然可以保留: 它没有调用控制台。

## 建议的重写顺序与观察方法

1. 先读懂一个状态字, 对照错误码; 暂不碰写盘。
2. 重写字符串解析, 用固定 WORD 数组验证结果。
3. 理解 IDENTIFY 的 512 字节数据, 打印行为放调用方。
4. 用现有镜像只读一个已知扇区, 比较 buffer 与磁盘内容。
5. 再加入多扇区、LBA48、范围校验和完成状态处理。

本次做了编译与链接依赖检查, 未做新的硬件执行验证。
参考文件保留原始控制台依赖, 所以全目录搜索仍会看到 puts;
检查正式驱动应针对 ata.c, 或检查正式目标文件的未定义符号。
