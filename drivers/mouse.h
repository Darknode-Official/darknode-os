#ifndef MOUSE_H
#define MOUSE_H
#include "../include/types.h"

void mouse_init(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_get_buttons(void);
void mouse_set_bounds(int w, int h);
void mouse_usb_update(int dx, int dy, int buttons);
void mouse_poll(void);
int mouse_debug_irqs(void);
int mouse_debug_polls(void);
int mouse_debug_packets(void);

#endif
