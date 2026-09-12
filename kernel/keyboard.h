#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "../include/types.h"

void keyboard_init(void);
char keyboard_getchar(void);
char keyboard_getchar_blocking(void);
uint8_t keyboard_last_scancode(void);
bool keyboard_has_key(void);
void keyboard_push_char(char c);

#endif
