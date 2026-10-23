# 命令行工具

`ls`、`cat`、`touch`、`sed`、`ed` 及以后新增的 `grep` 等独立文本程序放在这里。
CCP 只负责内置命令和外部程序启动，不包含这些工具的实现。

每个工具使用 `int main(int argc, char **argv)`，通过 CRT 绑定调用者的终端。
构建规则自动收集本目录下的 `.c`，链接文本运行库和文件系统库，生成
`bin/coreutils/<name>.exe`，并安装到 `fsroot/BIN/<NAME>.EXE`。
工具名须满足 FAT 8.3 的长度限制。

```sh
make coreutils       # 编译全部工具
make test-ls         # ls 的宿主测试
make test-cat        # cat 的宿主测试
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
