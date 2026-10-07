#include "ui.h"
#include "gfx.h"
#include "mouse.h"

void draw_button(PBUTTON p) {
    draw_rect(&p->R,(p->STATUS&1)?0x777777:0xc3c3c3);
}

void draw_drag(PDRAG p) {
    draw_rect(&p->R, 0xff0000);
}

int drag_mouse(PDRAG p, int x, int y, BYTE previous, BYTE buttons)
{
    int moved = 0;
    if ((buttons & MOUSE_LEFT) && !(previous & MOUSE_LEFT) &&
        is_inside(x, y, p->R.x, p->R.y, p->R.x+p->R.w, p->R.y+p->R.h)) {
        p->dragging = 1;
        p->offset_x = x - p->R.x;
        p->offset_y = y - p->R.y;
    }
    if (p->dragging) {
        int new_x = x - p->offset_x, new_y = y - p->offset_y;
        moved = new_x != p->R.x || new_y != p->R.y;
        p->R.x = new_x;
        p->R.y = new_y;
    }
    if (!(buttons & MOUSE_LEFT)) p->dragging = 0;
    return moved;
}
