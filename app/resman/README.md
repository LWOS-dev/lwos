# resman 图形与鼠标示例

`make` 生成 `RESMAN.BIN`，由 `loader/boot.ini` 预加载到 `0x180000`。
在 monitor 输入 `rm` 进入图形示例。保留原来的字体表和绿色矩形：

- 移动 PS/2 鼠标可移动光标，按任意鼠标键时光标变红。
- Esc 停止鼠标报告、恢复文本模式并返回 monitor；可再次输入 `rm`。
- Bochs 使用 `mouse: type=ps2, enabled=true`，必要时通过模拟器界面捕获鼠标。

## 鼠标接口

`lib/include/mouse.h` / `lib/src/mouse.c`，仅链接进 resman，使用 ABI 端口 IO。
调用顺序：`mouse_init()` → 循环 `mouse_poll()` / `mouse_read()` → `mouse_shutdown()`。

这是标准三字节 PS/2 流协议的轮询版本，不启用 IRQ12，也不需要 PIT。
`mouse_init()` 要求 monitor 已启用键盘，且初始化期间没有其他代码读取控制器。
初始化暂停键盘端口，处理 ACK/RESEND，限制等待次数，失败时尝试恢复配置。
轮询有读取次数上限，不读取键盘字节；错误状态会重置组包进度。
事件队列最多保留 31 个事件，满时丢弃最旧事件，保证最终按键状态能更新。
`dy` 正值表示向上，屏幕坐标使用 `y -= dy`。位移溢出时保留按键但丢弃位移。
当前不支持滚轮、USB 鼠标、拔插或设备复位后的自动重连。

GUI 示例直接消费翻译后的键盘扫描码，仅处理 Esc，不调用阻塞输入，
也不运行会与鼠标争用 ACK 的键盘 LED 命令。后续完整键盘输入应统一管理
PS/2 控制器命令及数据分发。鼠标轮询与中断接收不能同时启用。

场景保存在 backbuffer；光标只在显存上叠加，移动时从 backbuffer 恢复旧区域，
避免每次移动都复制整个屏幕。绘图像素寻址使用字节 pitch，当前只支持 32 bpp。

## 运行环境

monitor 校验映像头、首次清 BSS，并在原栈上调用 ENTRY；resman 暂时沿用
monitor 的中断环境。STACK_TOP 为零，不能使用 loader ATTACH/PORT 启动。
resman 第一次进入时初始化堆并分配后备缓冲，之后保留并复用。
当前分配器的物理元数据地址仍与 monitor 相同，切勿在二者之间同时保留并
交叉使用堆分配；后续需要把内存所有权完整迁入 resman。
尚未实现的 resman 服务 slot 为 null。

## 验证

- `make`
- `gcc -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -iquote lib/include tests/mouse_test.c lib/src/mouse.c -o /tmp/lwcnc-mouse-test && /tmp/lwcnc-mouse-test`
- `python3 tests/boot_test.py`：QEMU PC/Q35，检查鼠标位移、按键颜色、背景恢复、重复进入退出及视频切换。
