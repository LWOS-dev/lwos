# 命令行工具

`ls`、`cat`、`xxd`、`netrecv`、`touch`、`sed`、`ed` 及以后新增的 `grep` 等独立文本程序放在这里。
CCP 只负责内置命令和外部程序启动，不包含这些工具的实现。

每个工具使用 `int main(int argc, char **argv)`，通过 CRT 绑定调用者的终端。
构建规则自动收集本目录下的 `.c`，链接文本运行库和文件系统库，生成
`bin/coreutils/<name>.exe`，并安装到 `fsroot/BIN/<NAME>.EXE`。
工具名须满足 FAT 8.3 的长度限制。

```sh
make coreutils       # 编译全部工具
make test-ls         # ls 的宿主测试
make test-cat        # cat 的宿主测试
make test-xxd        # xxd 的宿主测试
make lwcon           # 编译宿主发送工具 build/tools/lwcon
make test-lwcon      # 宿主发送端丢 ACK/重试/失败回归
make test-net-init   # 82567LM 寄存器模型：D0/PHY/固件锁/IRQ 失败清理
make test-netrecv    # 82540EM IRQ + 文件落盘的 PC/Q35 集成测试
make test-touch      # touch 的宿主测试
make test-ccp-exec   # CCP 调用独立工具的 PC/Q35 集成测试
```

CCP 默认工作目录和搜索路径都是 `/BIN`。可以省略程序的 `.EXE` 后缀：

```text
[/BIN]$ ls -l
[/BIN]$ cd /
[/]$ cat README.TXT
[/]$ cd /BIN
[/BIN]$ cat ../README.TXT
```

内置命令：

- `pwd`：显示当前工作目录；CCP 内部变量称为 `cwd`。
- `cd DIR`：验证目录存在后切换，支持绝对路径、相对路径、`.` 和 `..`。
  无参数时返回 `/BIN`；失败时保留原目录。
- `path`：显示搜索路径。
- `path /BIN:/APP`：用冒号分隔的目录列表替换搜索路径；相对目录在设置时
  根据当前工作目录转成绝对路径。`path -` 清空搜索路径。

不含 `/` 的命令先在当前目录查找，再按 PATH 顺序查找；显式路径只查找指定位置。
没有扩展名时补 `.EXE`，已有扩展名则保留。工具通过 `lw_env_cwd(lw_env)` 获取
启动目录；未提供目录的旧版环境默认使用根目录。参数支持单引号、双引号及
反斜杠转义（单引号内保留正则表达式的反斜杠），暂不支持变量展开、管道或重定向。

`ls` 列出当前目录，保留名称列表、`-l` 目录标记和 `--` 支持，暂不接受路径参数。

`cat FILE` 读取一个文件并完整输出，支持 FAT 8.3 文件名及相对/绝对路径。
按 512 字节分块读取，不会把整个文件装入内存，也不追加换行。
当前不支持选项、标准输入、多文件或分页；超过屏幕高度的内容随终端滚动。

`xxd [OPTIONS] FILE` 流式显示文件的十六进制内容，默认每行 16 字节、每组
2 字节，左侧显示八位偏移，右侧显示 ASCII（不可打印字节显示为 `.`）。
支持 `-c COLS`（1–64）、`-g BYTES`（0–64，0 表示不分组）、`-s OFFSET`、
`-l LEN`、`-p`（纯十六进制，默认每行 30 字节）、`-u` 和 `-h`。
数字接受十进制或 `0x` 十六进制；数值选项可连写，如 `-l64`。
路径相对当前目录解析，`--` 后可使用以 `-` 开头的文件名。
当前不支持标准输入、输出文件参数、反向转换 `-r`、负偏移或其他上游选项。

```text
xxd /BIN/HELLO.EXE
xxd -g1 -s0x40 -l128 /BIN/HELLO.EXE
xxd -p -l32 /BIN/HELLO.EXE
```

## netrecv：通过裸以太网接收文件

先在目标机 CCP 输入保存路径（绝对路径或相对当前目录，FAT 8.3 名称）：

```text
[/BIN]$ netrecv /RECEIVED.BIN
netrecv: waiting for LWFT -> /RECEIVED.BIN
```

然后在直连宿主机上发送：

```sh
make lwcon
sudo build/tools/lwcon enp3s0 put ./program.exe
```

网卡名替换为连接目标机的接口；QEMU 使用 TAP 桥接接口。无需配置 IP。
这是 `0x88B5` 裸以太网协议，不能通过 QEMU user/slirp 网络转发。
宿主需 root 或 CAP_NET_RAW。发送端的文件名只是元数据，实际保存路径由
目标端参数决定；已有文件在接受 PUT 后截断覆盖，父目录可由 fs_touch 创建。
中途失败可能留下部分文件；不提供断点续传或原子替换。

每次命令接收一个文件，按 1024 字节停等发送。ACK 丢失时重发原序号，
重复 PUT/DATA 不重复截断或写入；长度和 CRC32 校验成功后确认 END。
接收端保留 2 秒响应重复 END，然后返回 CCP；30 秒无进展退出。
宿主每包等待 500 ms，最多尝试 8 次。空文件也支持。
本命令忽略 RUN/TEXT，不执行网络传来的程序或内存读写命令。

驱动在 resman 中，匹配 PCI `8086:100E`（82540EM，QEMU `e1000`）和
`8086:10F5`（82567LM，ICH9）。82540EM 已在 PC/Q35 验证；82567LM 当前为
MAC 复位、PHY 保留初始化，已通过寄存器模型测试，仍需真机验证发送。
82567LM 分支先恢复 PCI D0，关闭 MSI/MSI-X 并开启 MMIO/Bus Master/INTx；
关闭收发后取得 SWFLAG、等待 Master Enable Status 清零，执行 CTRL.RST，
保持 PHY_RST/LCD_RST 清零。复位后先睡眠 20ms（期间不读写 MMIO），
再有界等待 RST 清零和 EEC.Auto_RD，重新声明 DRV_LOAD 并配置队列。
配置由硬件自动从 NVM 加载，不执行 EERD 或写闪存。
取得 EXTCNF_CTRL.SWFLAG 后读取 PHY BMCR；无链路时清除 power-down/isolate、
低功耗链路状态并启动自动协商。保持已建立的 PHY 链路配置。
退出或失败时释放固件锁与驱动所有权。尚未移植完整 e1000e PHY/NVM 校准与复位流程。

使用 BIOS 已配置、未被其他处理函数占用的 PIC/INTx IRQ，暂不支持共享 IRQ
分发或 APIC 路由。无设备/IRQ 不可用/网卡已占用会报错。
同一时刻只允许一个任务占用网卡；命令结束或任务被关闭/kill 时释放。

真机先执行 `netrecv -d`（只初始化、自检和打印诊断，不创建文件）。输出包括：

- `PCI`、`BAR`、`CMD`、`PMCSR`：确认型号、D0 及 PCI 内存/总线主控使能。
- `LINK=UP/DOWN`：物理链路；诊断命令最多睡眠等待约 3 秒自动协商。
- `INTx-test=PASS/FAIL`：通过 ICS 触发中断并验证 ISR 实际到达，而不是仅安装处理函数。
- `FWSM`、`EXTCNF`、`PHY_CTRL`、`BMCR`：82567LM 固件/PHY 状态。
- `IRQs/RX/DROP/ICR/RXDESC`：接收超时也会再打印一次，便于定位收包或中断问题。
- `TX queued/done/busy`、`TDH/TDT/DESC`：本次打开以来的 TX 入队、描述符完成和
  队列忙次数，以及硬件头尾/最后描述符状态。`done` 表示硬件写回 DD，不代表宿主已确认。
- `TCTL/TXDCTL/TARC0`：发送使能、描述符写回策略及 ICH 发送仲裁寄存器。
- `TCTL-reset/TARC1`：复位加载后的 TCTL 快照和队列 1 的仲裁配置。
  启停 TX 保留 TCTL 的 MULR 和 RRTHRESH，高位不再被整寄存器清零覆盖。
  TARC1 bit28 根据实际 TCTL.MULR 取反配置。
- `TDBAH/TDBAL/TDLEN/ring`：TX 环基址读回、长度（字节）及软件预期地址。
  当前应为 `TDBAH=0`、`TDBAL=ring`、`TDLEN=256`。
- `CTRL_EXT/EEC/PBA/PBS`：扩展控制、NVM 自动加载完成状态及片内缓冲区分配。
- `TX0 addr/cmdlen/status`：首个 TX 描述符的缓冲地址、命令/长度及写回状态。
  单帧 ACK 的 `cmdlen` 应为 `0b00003c`；硬件完成后 `status` 的 DD 位为 1。

`invalid PIC IRQ route` 表示 PCI IRQ line/pin 不适用于当前 PIC 实现；
`IRQ already occupied` 表示该向量已有处理函数；`INTx self-test failed` 表示
驱动尚未收到测试中断，不能据此认定 PHY 未上电。
`firmware/SWFLAG busy`、`PHY MDIO failed`、`invalid MAC address` 分别指出
固件所有权、MDIO 访问或 BIOS 未提供有效 MAC 的问题。
`ready` 只表示初始化和 IRQ 自检通过，链路仍以 `LINK` 字段为准。
`DMA master stop timeout`、`MAC reset timeout`、`NVM auto-load timeout`
分别指出 DMA 停止、MAC 复位或自动加载没有在有界等待内完成。

如果出现 `LINK=UP`、`INTx-test=PASS`、`RX` 增加，且文件已创建/截断，
就说明 PUT 已经到达并被接受；此时宿主超时在 `ACK seq 0` 应检查 ACK 返回路径。
82567LM 分支显式配置两组 TXDCTL、ICH TARC、单描述符写回和禁用固件遗留 TX 队列，
避免仅初始化 RX 后收得到 PUT、却无法发出 ACK。
TX 环独立使用 16 个描述符（256 字节），符合 ICH SDM §10.4.58 的最小尺寸；
此前的 8 个描述符虽能通过 QEMU 82540EM 测试，却不符合 ICH9 的要求。
`make test-net-init` 现检查这个限制，以及 TX 独立回绕、单帧完成和队列满。
真机修正尺寸后仍出现 TDH 不推进，因此改为在重建环前复位 MAC 数据通路，
清除固件遗留的片内队列状态。
随后真机确认复位/自动加载完成、TX0 命令/长度正确，但仍无 TX 完成。
代码审查发现复位之后整寄存器写 TCTL=0、启用时写旧型号常量，
会抹掉复位/NVM 加载的高位策略。现改为读改写保留这些位，CT=15、
COLD=63、启用 RTLC，并按 MULR 配置 TARC1；实际硬件发送仍待确认。
`netrecv` 接受 PUT 时打印确认，超时打印协议阶段、字节进度、回复入队及失败次数。
宿主超时只表示未收到有效 ACK；新版 lwcon 会打印收包和过滤计数，不再据此认定从机没运行。
可在宿主另开终端执行 `sudo tcpdump -ni IFACE -e -XX ether proto 0x88b5`，
将 IFACE 替换为同一个有线接口，以检查 PUT 和 ACK 两个方向。

IRQ 读取 ICR、搬运完成的 RX 帧到内核队列并唤醒 TASK_WAIT_NET；空队列时
接收任务原子阻塞，PIT 只处理超时，不轮询 RX。文件写入、协议和 CRC 都在任务中完成。
DMA 环和缓冲区在 resman 的静态 BSS 中，不再使用旧驱动的 `0x00340000` 固定地址。
旧 monitor 的 `n` 检查网卡、`nr` 调试接收也改为共用这一中断接口，保留 lwcon `-s` 用法。

协议头及 CRC 共用 [lwft.h](../../lib/include/lwft.h)。所有负载字段为小端：
PUT 的 seq=0、arg=总字节数、data=源文件名；DATA 的 seq 逐包递增、arg=字节偏移；
END 的 arg=CRC32；ACK 回显 seq、arg=已保存字节数；NAK 的 arg=负错误码。
网卡寄存器和描述符依据 [Intel 8254x SDM](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf)。
82567LM 的固件所有权和 PHY 电源操作另参考 Intel 维护的
[e1000e ICH 实现](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)
及 [PHY 电源操作](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/phy.c)。
ICH 的 TX 环尺寸依据 [Intel ICH8/9/10 SDM §10.4.58（镜像）](https://manuals.plus/m/2843bad10cf512859b9602579f8d888ef2d7c9b8a482165a324a5e0d42f21693)。
MAC 软件复位与独立 PHY 复位的区别见同一手册 §11.2；20ms 后再访问寄存器的
顺序参考上述 e1000e `e1000_reset_hw_ich8lan` 实现。
TCTL 高位默认配置参见同一手册 §10.4.52；启用发送时保留已有字段的方式
也见 [iPXE intel_open](https://github.com/ipxe/ipxe/blob/master/src/drivers/net/intel.c)。

`touch [--] FILE...` 创建一个或多个空文件；已有文件的内容和长度保持不变。
支持当前目录下的相对路径及绝对路径，父目录必须存在，不会自动创建目录。
`--` 后可传入以 `-` 开头的文件名。个别文件失败时会继续处理其余文件，并返回错误。
当前文件系统没有时间戳更新接口，因此这版只实现创建/保留文件，不更新时间戳，
暂不支持 `-a/-m/-c/-t` 等选项。

```text
touch NOTE.TXT
touch /DOC/A.TXT /DOC/B.TXT
```

## sed

移植自 minised 1.15，保留其流编辑和基本正则引擎。支持 `-n`、`-e`、`-f`、
行号/正则地址、范围、替换与反向引用、`p/d/q`、hold space、分支及 `r/w` 文件操作。
不带输入文件时逐行读取当前终端，按 Esc（或事件源提供的 Ctrl-D）结束输入。

```text
sed 's/old/new/g' /INPUT.TXT
sed -n '2,5p' /INPUT.TXT
sed -f /SCRIPT.SED /INPUT.TXT
sed -n 's/old/new/g;w /OUTPUT.TXT' /INPUT.TXT
```

这不是 GNU sed；不支持 `-i`、`-E`。保留上游限制：pattern/hold space 为 4000
字节、最多 200 条编译命令等；详细语法见 `vendor/minised/minised.1`。

## ed

移植自 BusyBox 的轻量行编辑器，在当前终端交互，文件内容在内存中编辑，`w` 保存。
支持 `-s`、`-p PROMPT`，行号、`.`、`$`、逗号范围、标记和字符串查找，
以及 `a/i/c/d/p/l/s/r/w/f/k/z/=/q` 等上游命令。`s` 和查找使用**字面字符串**，
不支持正则、undo、global 或 shell escape。

```text
ed -p 'ed> ' /NOTE.TXT
ed> 1,$p
ed> 2s/old/new/g
ed> a
new line
.
ed> w
ed> q
```

新文件也可以直接 `ed`，用 `a` 输入多行，以单独的 `.` 结束，再 `w /NEW.TXT`。
`w` 会覆盖并截断旧文件，支持保存空缓冲区。存在未保存修改时，`q` 会询问是否退出。
终端输入每行最多 1022 字节。Esc 是 EOF，不会自动保存；命令行 `q` 退出更合适。

两者均继承当前目录，沿用 FAT 8.3 路径规则。上游版本、许可和本地修改见
[vendor/UPSTREAM.md](vendor/UPSTREAM.md)。集成测试运行在临时磁盘副本上，并逐字节校验保存结果。
