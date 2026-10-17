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
