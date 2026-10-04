# mkexe

`mkexe.c` 留给亲手实现, 当前只有一个返回失败的入口。
之前 AI 生成的框架原样保存在 `mkexe_reference.c`, 用来阅读和比较。
它不是完整转换器, ELF 转换和 LWP 写出仍未实现。

阅读说明与例子见 [WALKTHROUGH.md](WALKTHROUGH.md)。

```sh
make -C tools/mkexe            # 构建自己的 mkexe
make -C tools/mkexe reference  # 单独构建学习参考
build/tools/mkexe-reference --help
build/tools/mkexe-reference -d example.lwp
make -C tools/mkexe test-elf    # 编译裸机验证程序, 不执行
```

共享磁盘格式为 `lib/include/lwp.h`, 工具接口为 `mkexe.h`。
`lwp.ld` 从地址 0 链接, 入口是 `lwp_main`, 链接时保留重定位。
`test.c` 也是 AI 生成的验证示例, 可以按自己的理解重写。

monitor 的 `.` 当前读取一个扇区到 0x400000, 可以查看 LWP 文件头。
完整加载需要读全映像和重定位表, 再由加载器分配内存、清零 BSS、
修正地址并调用入口。读取文件本身不会执行程序。
