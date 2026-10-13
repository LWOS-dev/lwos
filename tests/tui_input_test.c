/* 宿主回归：验证实际地址输入事件和显存中的结果，不调用裸机 ABI。 */
#include <assert.h>
#include <stdio.h>
#include "../app/monitor/src/tui.c"

static WORD screen[80 * 25];

static void expect_address(PTUI_AREA area, int row, PCSTR expected)
{
    for (int i = 0; i < 8; i++)
        assert((BYTE)screen[(area->y + 1 + row) * 80 + area->x + 7 + i] == expected[i]);
}

int main(void)
{
    PCSTR input = "  1234aBcD rest";
    DWORD number = 0;
    assert(hex_parse(&input, &number) && number == 0x1234abcd);
    assert(*input == ' ');
    input = "FFFFFFFF";
    assert(hex_parse(&input, &number) && number == 0xffffffff);
    assert(*input == 0);
    input = "G";
    number = 42;
    assert(!hex_parse(&input, &number) && number == 42);
    assert(hex_digit(':') == 0xff && hex_digit('/') == 0xff);
    assert(hex_digit('g') == 0xff && hex_digit('Z') == 0xff);

    video = screen;
    focus = 0;
    TUI_AREA area = {.x = 0, .y = 1, .index = 0};
    proc_mr(&area, VK_RIGHT); /* 地址列 */
    proc_mr(&area, VK_F4);
    for (PCSTR p = "1234aBcD"; *p; p++) proc_mr(&area, *p);
    expect_address(&area, 0, "1234ABCD"); /* 第八位立即提交 */
    assert(type_ptr == 8 && type_buf[8] == 0);
    proc_mr(&area, '9');
    expect_address(&area, 0, "1234ABCD"); /* 提交后不再追加 */

    proc_mr(&area, VK_F4);
    proc_mr(&area, '4');
    proc_mr(&area, '2');
    proc_mr(&area, VK_F4);
    expect_address(&area, 0, "00000042"); /* 短输入手动提交 */
    proc_mr(&area, VK_F4);
    proc_mr(&area, 'G');
    assert(type_ptr == 0);
    proc_mr(&area, VK_F4);
    expect_address(&area, 0, "00000042"); /* 无效/空输入保留旧地址 */

    proc_mr(&area, VK_F4);
    proc_mr(&area, 'A');
    proc_mr(&area, VK_DOWN);
    proc_mr(&area, VK_F4);
    expect_address(&area, 0, "0000000A"); /* 始终提交到开始编辑的行 */
    expect_address(&area, 1, "00000000");
    puts("PASS: hex parsing and TUI address input/commit/display");
    return 0;
}
