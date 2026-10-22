# 外部 C 程序

这里的程序与 `tools/mkexe/test.c` 一样，由 resman 装载运行，不固定装载地址。
入口为 `int lwp_main(const LW_ENV *env)`，运行时定义取自 `lib/include/resman.h`。
通过 `env->abi` 使用公开 ABI，不链接 ABI.BIN 的实现，不使用宿主 libc/CRT。

在仓库根目录运行：

```sh
make external       # C -> ELF (保留重定位) -> LWP EXE
make fsroot         # 复制到 fsroot/REDDOT.EXE
make                # 更新启动镜像
```

链接复用 `tools/mkexe/lwp.ld`，产物为 `bin/external/reddot.elf` 和
`bin/external/reddot.exe`。在 monitor 文件列表里选中 `REDDOT.EXE`，按 F4 运行。

`reddot.c` 使用新增的 `lw_tramp_call`（ABI slot `0x80`）调用 BIOS INT 10h：
切到 VGA 13h 模式，在左上角 `(0,0)` 画一个红色像素，等待 `lw_getk()`，
再用 `lw_gfx_exit()` 恢复 80×25 文本模式并返回 monitor（界面由 monitor 重绘）。
它需要与本次更新后的 ABI.BIN 一起使用；原文本显存内容不作快照保存。

新增单文件程序时，将源码放在此目录，将名称加入 `build.mk` 的
`EXTERNAL_NAMES`，并按 REDDOT 的示例登记镜像文件名和来源。

## XTAL 水晶渲染

`xtal.c` 转自 `04.crystal cluster.glsl` 中启用的 `scene2`：72 根水晶，
保留色散、两次反射和六层透射。使用单精度 `float`，sin/cos、sqrt 和
着色器中的幂函数通过 `xtal_lut.h` 插值计算，不依赖运行时数学库。
LUT 可用 `python3 tools/gen_xtal_lut.py` 离线重新生成。

运行镜像中的 `XTAL.EXE`（F4）。A/D 左右旋转，W/S 上下旋转，R 复位，
其他键前进一个旋转角度，Esc 恢复文本模式并退出。初始画面对应原 shader
的 `time=0`、鼠标位于中心；当前没有将系统时间和鼠标接入 shader。
每次按键重绘一帧，不是实时连续动画。

CPU 渲染默认按约 320×200 的采样密度填充像素块，保持屏幕宽高比。
需要逐像素渲染时可将 `lwp_main` 中的 `step` 固定为 1，耗时相应增加。
显示适配 24/32 bpp 的常见 BGR 字节布局（与 `shader.c` 一致），按 pitch
计算每行地址；公开 ABI 尚未提供 RGB 位位置查询。

`make test-xtal` 执行宿主 LUT 误差和两组视角的渲染检查；
`build/tests/xtal_test /tmp/xtal.ppm` 另存首帧预览。

## A/B 抢占式多任务与 x87 保存验证

`make` 会把 `a.c`、`b.c` 编译为镜像根目录的 `A.EXE`、`B.EXE`。
启动 `make run`，先按 Esc 退出自动启动的 GHOST，再按任意键通过 monitor 启动等待：

1. Tab 切到文件浏览器，分别选中 `A.EXE`、`B.EXE`，各按一次 **F7** 后台启动。
2. Tab 回到左侧菜单，选 **TASKS**，按 Enter。
3. 页面按 **ID + NAME** 区分任务；任务 0 是 MONITOR。**TICKS** 为调度时钟计数，
   **WORK** 为程序自己的循环计数（十六进制，32 位溢出回绕）。按 **F5** 刷新观察变化。

A/B 使用 `fpu_check.h`：A 向 ST(0)–ST(7) 放入八个不同的正数，
B 放入八个不同的负数，并使用不同的舍入模式、TOP 和条件标志。
预期的 108 字节状态由整数代码构造，通过 `frstor` 装入；随后保留寄存器内容，
在开启中断的忙循环之后用 `fnsave/frstor` 取得快照，逐字节核对八个 80 位值，
并核对控制字、状态字和标签字。保留位、指令和数据指针不参与比较。
每次核对成功，A 给 WORK 加 1，B 加 2；两个循环都没有 sleep/yield。
PIT 使用 1194 分频（约 999.315 Hz）触发 IRQ0，普通抢占每 10 tick 执行一次，
定时等待到期则立即调度，通过 TSS far JMP 轮转 READY 任务，
因此 A、B 都持续运行，MONITOR 也仍能响应键盘。同一个文件启动多次会得到不同 ID。
F4 保持原来的同步执行行为；这两个死循环请用 F7 启动。

验证通过时，两项任务保持 READY/RUNNING，WORK 持续增加。
发现不一致时，任务变为 DEAD，WORK 显示 `800000xx`，退出码为 `-xx`：

| xx（十六进制） | 不一致的内容 |
| --- | --- |
| 01 | 控制字（含舍入模式） |
| 02 | 状态字（含 TOP 和条件标志） |
| 03 | 标签字 |
| 10–17 | ST(0)–ST(7) 的 80 位内容 |

本验证覆盖当前 `fnsave/frstor` 使用的 x87 状态，不覆盖 SSE/AVX。
调度器在切换前保存/恢复状态，并在新任务入口和旧任务恢复处清除 CR0.TS。
共享 ABI/文件系统仍没有并发保护，暂不使用 F7 运行图形或并发 I/O 程序。
任务返回后记录 DEAD 和退出码；在 TASKS 页回收后释放映像、栈和任务槽。
死循环可以在 TASKS 页终止，无需重启。

回归验证：`python3 tests/task_schedule_test.py`，在 QEMU PC/Q35 上通过实际 F7 操作
连续五次采样检查 A/B 校验计数和三个任务的时钟计数持续推进、任务没有 DEAD，
以及 TASKS 页面仍可操作。运行前先 `make` 更新镜像。

## 任务生命周期

状态路径为 `FREE -> CREATING -> READY <-> RUNNING -> DEAD -> FREE`。
创建失败会回滚预留任务槽；成功创建后映像由任务持有，不能直接卸载。

- 普通 `return code`：入口包装函数调用 `resman_task_exit(code)`，记录退出码并切走。
- `rm_task_exit(code)`：当前后台任务主动退出，成功时不返回；任务 0 调用返回错误。
- `rm_task_kill(id)`：终止另一个 READY/WAITING 任务，退出码为 `-128`。
  不能终止任务 0、自己、空槽、创建中或已经 DEAD 的任务；结束自己用 exit。
- `rm_task_reap(id, &code)`：非阻塞回收。只有 DEAD 且不在执行的任务可回收。
  返回退出码，并释放映像、栈、GDT 描述符、私有运行时/FPU 保存区和任务槽。
  活任务、重复回收返回错误；失败不改写输出退出码。

退出/终止本身不释放内存；必须显式 reap。这样既能保留退出结果，也避免在自己的
栈上运行时释放自己。回收操作关中断，并要求 TSS 描述符为 available；不在 IRQ 中回收。
任务 ID 仍是可复用的槽位编号，reap 后旧 ID 失效，不应再用于操作后来创建的任务。

TASKS 页操作：

1. Tab 切到任务表（焦点 1），上下键选择，高亮显示选中任务；列表可滚动。
2. F8 终止选中任务，状态变为 DEAD，EXIT 显示退出码（十六进制）。
3. F9 回收 DEAD 任务，行从表中消失；底部 REAP EXIT 保留最近一次回收的退出码。
4. 返回文件浏览器用 F7 再次启动，可复用刚释放的槽位。

`TEST.EXE` 后台启动后自然返回 42；`EXIT.EXE` 检查拒绝非法生命周期操作后主动退出，
退出码为 7。F4 同步运行 EXIT.EXE 时，退出任务 0 的请求被拒绝，程序正常返回 7。
`python3 tests/task_schedule_test.py` 同时验证这些路径、活任务拒绝回收、任务 0 保护、
反复创建回收后的分配位图/GDT 不泄漏，并保留 A/B 的 x87 抢占检查。

这一版回收任务的映像、初始栈和 rm_alloc 分配的内存；打开的文件和设备状态尚无
按任务归属的自动清理。共享 ABI/文件系统没有并发保护，强制终止先用于 A/B 这类
纯计算任务；阻塞 wait/join、父子任务关系及通用资源清理留待后续实现。

## 应用 alloc/free

初始化 `rm_base = env->rm` 后使用：

```c
PVOID *rm_base; /* 每个 EXE 定义一次 */

char *buffer = rm_alloc(4000, 0, 0); /* size, alignment, tag */
if (!buffer) return -1;
buffer[0] = 'A';
buffer[1] = 0;
rm_free(buffer);
```

不需要链接 mem.c 或初始化内存池。返回内存未清零，最小占用 4096 字节。
align=0 为默认对齐，也可指定 1..4096 的二次幂；失败只返回 NULL。
只能释放本任务申请的原始地址，NULL 可释放；错误返回 RM_ERR_ARGUMENT。
后台任务未释放的内存在 F9/reap 时回收。F4 前台程序属于任务 0，必须自己释放。

镜像根目录的 MEMTEST.EXE 检查参数、缺页空间时的分配失败、数据读写、重复/错误
释放与嵌套关中断恢复；F7 正常退出码为 29（十六进制 1D），故意保留的两页由 F9 回收。
MEMLOOP.EXE 持续分配、校验并释放临时缓冲区，可启动两份，通过 WORK 观察进度，
再 F8/F9 终止回收。共享内存的所有权转移不包含在本接口中。


## GHOST 文本窗口最小闭环（C0）

通用加载入口声明在 `lib/include/app.h`，实现在 `lib/src/app.c`：

```c
DWORD task_id = 0;
int rc = lw_app_launch(lw_blk_get(0), "/TEXTDEMO.EXE", 0, &task_id);
```

它负责文件读取、映像加载、任务创建及失败清理；成功后由调用方管理任务，
退出后调用 `rm_task_reap`，仍运行时先 `rm_task_kill`。调用方需初始化 `rm_base`，
串行访问磁盘，且 TEXT 程序仍要求已注册文本 host。当前保留 1 MiB 文件大小上限。
链接需包含 APP 库的 `app.o`、`fs.o` 及其依赖；GHOST 已配置。
路径、单窗口限制、焦点和任务关闭策略由 GHOST 决定；加载库不依赖 demo 名称或图形接口。
`make test-app-launch` 验证参数、加载失败清理和成功后的映像所有权转交。

运行 `make` 更新镜像后启动 `make run`，进入 GHOST：

- **T**（桌面焦点）：加载根目录 `TEXTDEMO.EXE`，打开并聚焦一个 80×25 文本窗口。
- **C**（桌面焦点）：关闭窗口，终止尚在运行的示例并回收任务/映像/缓冲区。
- **Esc**（桌面焦点）：先清理示例，再退出 GHOST 返回 monitor。
- 单击窗口获得焦点，单击桌面取消焦点；标题栏颜色显示焦点状态。

`textdemo.c` 通过 `env->vi.txt->buffer` 写入字符/颜色，等待窗口事件并回显输入，Enter 返回 42。
标题显示十六进制退出码 `0000002A`；内容保留到关闭窗口。
它不调用分配器或绘图接口，也不接触 framebuffer。构建通过 `mkexe --text` 设置 `flags=0x10`。

resman 加载时分配字符缓冲区，创建任务时注入环境；GHOST 定期调用
`rm_text_snapshot` 拷贝字符和任务状态，再交给 GUI 库用内置字体绘制。快照不暴露共享指针，
复制期间禁止任务切换，绘图期间无需持有 resman 资源。GHOST 在进入图形模式后注册、退出时注销。
无 GUI host 时 TEXT 加载被拒绝，F4 同步执行 TEXT 暂不支持；旧程序 flags=0 保持原行为。

首版只管理一个示例窗口，保留 DEAD 任务及其映像直到关闭，不支持提前 reap 后继续保留窗口。
CRT/main/stdio、stdin 行编辑、拖动、多窗口和 Z 序属于后续阶段。
`python3 tests/text_window_test.py` 在 QEMU PC/Q35 对比完整客户区的字形/颜色，
检查焦点切换、按键隔离、等待/唤醒、退出码、窗口代次、反复关闭后的内存位图和冲突 flags 的失败回滚。
截图输出到 `build/text-window.ppm`。


## 窗口事件与焦点

`lib/include/window.h` 定义 `LW_WINDOW_EVENT`，resman 在 ABI 表末尾追加：

- `rm_window_send(window_id, &event)`：仅注册的 GHOST host 可发送。类型为
  `LW_WINDOW_KEY`（value 为现有字符/VK 码）或 `LW_WINDOW_FOCUS`（0/1）。
- `rm_window_poll(window_id, &event)`：仅窗口所属任务可读取；1 表示事件，
  0 表示队列为空，负值表示错误。
- `rm_window_wait(window_id)`：所属任务在队列为空时原子阻塞；新事件唤醒。

每窗口独立 FIFO 可容纳 31 个事件，满时返回 `RM_ERR_NOT_READY`，不覆盖旧事件。
发送使用带代次的窗口 ID，拒绝已退出窗口和旧 ID；键盘事件不包含鼠标按钮。
焦点保存在 GHOST 的 `focus_window`，resman 只负责投递、权限检查与等待/唤醒。
应用仍不取得全局键盘所有权。焦点窗口收到全部键盘事件，包括 C、T、Esc；
如需 GHOST 快捷键，先点击桌面。焦点通知与按键共用有序队列。
窗口退出、关闭或失效时，GHOST 清除焦点；当前仍只管理一个窗口，没有 Z 序。

`make test-window-event` 检查 FIFO 边界、读写权限、无效事件和失效窗口 ID。
`python3 tests/text_window_test.py` 覆盖完整 GHOST 输入链路。


## GUI 绘制库

`lib/include/gui.h` / `lib/src/gui.c` 提供单显示器渲染接口，GHOST 只传入
窗口位置、标题、文本快照和焦点状态：

```c
GUI_TEXT_WINDOW window = {16, 48, "My application"};
gui_draw_text_window(&window, &snapshot, focused);
gui_present();
```

- `gui_open` / `gui_close`：图形模式、后备缓冲区和字体的初始化与释放。
  调用方先初始化 ABI 指针，`GUI_DISPLAY` 用 `{0}` 初始化；底层 gfx 为全局状态，
  同时只支持一个显示器实例。
- `gui_draw_desktop` / `gui_draw_error`：桌面和状态提示。
- `gui_draw_text_window` / `gui_erase_text_window`：窗口边框、焦点标题栏、
  退出码、80×25 字符及颜色绘制，以及关闭后的区域清除。
- `gui_text_window_rect` / `gui_text_window_contains`：共享窗口尺寸计算和命中判断。
- `gui_draw_pointer` / `gui_present`：鼠标标记和画面提交。

字体、调色板和窗口装饰尺寸集中在 GUI 库；库不查询任务、修改焦点或加载应用。
GHOST 链接 APP 库的 `gui.o` 及现有 gfx/mem/convert 依赖。
`python3 tests/text_window_test.py` 验证抽取后的完整客户区像素、焦点和关闭回收。


## 毫秒 sleep

初始化 `rm_base = env->rm` 后直接调用，无需链接额外库：

```c
DWORD started = rm_uptime_ms();
int rc = rm_sleep(1);  /* 阻塞当前任务，最小正数请求为 1 ms。 */
DWORD elapsed = rm_uptime_ms() - started;
```

`sleep(0)` 让出 CPU；正数请求使用 `TASK_WAIT_TIMER` 和 `wake_tick`，不忙等。
调用需要处于运行任务且中断开启，否则返回 `RM_ERR_NOT_READY`；超出
`RM_SLEEP_MAX_MS`（0x7ffffffd）的请求返回 `RM_ERR_ARGUMENT`。
截止 tick 的比较支持 32 位回绕；`rm_uptime_ms()` 也会在约 49.7 天回绕，
应使用无符号减法计算短时间间隔。CPU `run_ticks` 现在每个 PIT tick 增加。

PIT 时钟 1193182 Hz，分频 1194，一个 tick 约 1.000686 ms。
为覆盖调用恰在下次 IRQ 前的相位差，正数请求按 `ms + 1` 个 tick 等待。
空闲时 `sleep(1)` 通常约 1–2 ms；更长请求还包含 PIT 分频误差。
调度竞争、关中断及虚拟机负载会延长等待，这个接口不承诺实时截止精度。
普通抢占仍约 10 ms；到期睡眠任务在 1 ms 计时 tick 上被唤醒并参与调度。

`python3 tests/sleep_test.py` 在临时镜像上验证阻塞、idle、键盘不唤醒定时等待、
禁中断/越界参数、sleep(0) 及 tick 回绕，并以 PIT 校准 TSC，打印 1/5/10 ms
请求各 32 次采样的最小和最大延迟。测试结果是 QEMU TCG 测量，不代表真机上界。
