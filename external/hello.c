#include "stdio.h"

int main(void) {
    printf("Hello, world!\n");
    printf("signed=%d unsigned=%u hex=%08x\n", -123, 123u, 0x1234u);
    puts("Each program has its own stdout and cursor.");
    return 0;
}
