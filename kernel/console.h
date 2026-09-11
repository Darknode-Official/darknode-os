#ifndef CONSOLE_H
#define CONSOLE_H
#include "../include/types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

void console_init(void);
void console_clear(void);
void console_putchar(char c);
void console_write(const char *s);
void console_write_color(const char *s, uint8_t color);
void console_write_dec(uint32_t n);
void console_write_hex(uint32_t n);
void console_set_color(uint8_t fg, uint8_t bg);
void console_set_cursor(int x, int y);
void console_scroll(void);
void console_newline(void);
void console_backspace(void);
int  console_get_x(void);
int  console_get_y(void);

#endif
