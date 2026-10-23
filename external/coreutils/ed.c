#include "port/compat.h"
int ed_main(int argc,char **argv);
int main(int argc,char **argv) {return port_run(ed_main,argc,argv);}
