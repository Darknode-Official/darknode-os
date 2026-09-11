#ifndef MOUSE_H
#define MOUSE_H
#include "../include/types.h"

void mouse_init(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_get_buttons(void);
void mouse_set_bounds(int w, int h);

#endif
