#include "stdio.h"

int main(int argn, char** argv) {
    printf("Hello, world!\n");
    for (int i=0; i<argn; i++) {
        printf("ARG[%d]=\"%s\"\n",i,argv[i]);
    }
    return 0;
}
