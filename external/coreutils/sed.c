#include "port/compat.h"
int sed_main(int argc,char **argv);
int main(int argc,char **argv) {return port_run(sed_main,argc,argv);}
