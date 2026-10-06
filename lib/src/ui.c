#include "ui.h"

void draw_button(PBUTTON p) {
    draw_rect(p->X,p->Y,p->W,p->H,(p->STATUS&1)?0x777777:0xc3c3c3);
    
}


void draw_drag(PDRAG p) {
    draw_rect(p->X,p->Y,p->W,p->H, 0xff0000);
}