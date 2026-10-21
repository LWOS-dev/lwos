# 60 — 图形界面路线

## 目标

从"能画像素"一步步走到"多个窗口、鼠标拖动、monitor 在窗口里运行"。
**每一步都以一个 monitor 命令结束，敲下去就能看到效果**。这样中途停下来，回来时先跑一遍 demo，就知道做到哪了。

## 现状

- VBE 线性帧缓冲可用：`lw_gfx_enter`，Bochs 下 1280×1024×32。
- `LW_SLOT_INFO_FB_PITCH`（0x34）已加。
- `app/monitor/src/monitor.c` 里有 `putpixel` 和画曲线的 demo，都直接写 LFB。

## 依赖

- G0–G1：无。**现在就能做。**
- G2：需要 5MB 内存做后备缓冲。有 [50](50-resman-memory.md) R2 就用 `rm_alloc`；没有的话先临时写死一个地址（例如 0x800000，Bochs 32MB 下可用），**并在代码里标注 TODO**。
- G3：PS/2 鼠标可以**轮询**，不需要等中断。bochsrc 要改成 `mouse: type=ps2, enabled=true`。
- G6：[51 任务](51-tasks.md)。
- G7：[41 DLL](41-dll.md)。

## 设计

### GHOST 文本窗口

G5/G6 的应用文本输出按 [61 文本窗口与标准输出](61-text-console-stdio.md) 推进：
resman 分配并注册 80×25 字符缓冲区，通过 `LW_ENV.vi.txt` 交给应用；GHOST 将字符绘制到
窗口客户区并合成到 framebuffer。先跑通直接写字符的 TEXTDEMO，再接 CRT、stdio 和 printf。
应用窗口只为显式请求的程序建立；不要求所有后台任务都带窗口。

### 放在哪

```
lib/gfx/                    静态库；只用整数运算，KERN 和 APP 都能链接
  include/gfx.h
  src/surface.c             画布、填充、线、拷贝
  src/font.c                文字
  src/font8x16.c            字体数据（生成的）
```

先做静态库，链接进 monitor。等 DLL 做好，把它编成 GFX.DLL（G7）。
**不要把画图函数加进 ABI 表**：slot 永久占号，画图接口在稳定之前一定会改。

### 画布

```c
typedef struct {
    PDWORD px;
    int    w, h;
    int    pitch;      /* 以 DWORD 为单位 */
} GFX_SURFACE;
```

屏幕是一个画布（`px = lw_get_fb()`，`pitch = lw_get_fb_pitch()/4`），后备缓冲是另一个。
所有画图函数都带 `GFX_SURFACE *` 参数，**不用全局变量**（为 DLL 做准备，见 [41](41-dll.md)）。

### 像素格式

BINFO 里有红绿蓝的位置和宽度（`fb_redpos` 等）。第一版假设 32bpp、`0x00RRGGBB`，
在 `gfx_init` 里**检查** redpos=16、grnpos=8、blupos=0，不符合就打印警告。

### 字体

8×16 位图，每个字符 16 字节，256 个字符共 4KB。两个来源：

1. 通过 BIOS 跳板调 `INT 10h AX=1130h BH=06h`，从 ES:BP 拿到 VGA ROM 字体的地址。
2. 用 monitor 把这 4KB dump 出来，在宿主端转成 `font8x16.c`。之后就不再依赖 BIOS。

推荐第 2 种。

### 后备缓冲

- **不要读 LFB**。显存通常不走缓存，读非常慢。
- 所有绘制都画到内存里的后备缓冲，再把**脏矩形**拷到 LFB。
- 拷贝时按行 `memcpy`，并注意源和目标的 pitch 不同。

### 鼠标（PS/2）

- 8042 的第二个口（aux）：向 0x64 写 0xA8 启用；向 0x64 写 0xD4，再向 0x60 写 0xF4，开始上报数据。
- 轮询：状态口 0x64 的 bit0 = 有数据，**bit5 = 数据来自鼠标**（否则是键盘）。
- 3 字节一个包：按键 + X 位移 + Y 位移（带符号位；Y 轴向上为正）。
- **会和键盘驱动冲突**：`kbd_poll` 现在可能会把鼠标字节当成键盘扫描码读走。鼠标上线时，要么在 `kbd_poll` 里判断 bit5，要么把 0x60 的读取统一到一处再分发。

## 里程碑

| # | 内容 | demo（monitor 命令） |
|---|---|---|
| G0 | `GFX_SURFACE`、`fill_rect`、`hline`、`vline`，按 pitch 寻址 | `g0`：画几个彩色方块，边缘对齐，没有斜切 |
| G1 | 8×16 字体 + `draw_text` | `g1`：方框里显示 "hello LWOS" |
| G2 | 后备缓冲 + 脏矩形刷新 | `g2`：一个方块在屏幕上来回移动，不闪 |
| G3 | PS/2 鼠标轮询 + 光标 | `g3`：鼠标移动光标，按键时屏幕角落显示按键状态 |
| G4 | 窗口：链表、z 序、标题栏拖动 | `g4`：两个重叠窗口，可以拖，点击时置顶 |
| G5 | 文本窗口 + 控制台重定向 | `g5`：monitor 的输出出现在一个窗口里 |
| G6 | 请求 TEXT 的任务各有文本窗口，GHOST 并发合成 | 两个任务各自输出；具体闭环与生命周期见 61 的 C0–C4 |
| G7 | GFX.DLL | monitor 和一个 EXE 通过同一个 DLL 画图 |

## 待决

- 中文：要 16×16 点阵字库（GB2312 大约 260KB），放到 G5 之后再说。
- 1280×1024 的分辨率对 CNC 屏幕来说合不合适？模式选择在 stage2 里，可以以后再调。
