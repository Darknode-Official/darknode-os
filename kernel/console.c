#include "console.h"
#include "io.h"
#include "string.h"

static uint16_t *vga = (uint16_t *)VGA_MEMORY;
static int cur_x = 0, cur_y = 0;
static uint8_t cur_color = 0x07;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void update_cursor(void) {
    uint16_t pos = cur_y * VGA_WIDTH + cur_x;
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void console_init(void) {
    cur_x = 0;
    cur_y = 0;
    cur_color = 0x07;
    console_clear();
}

void console_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = vga_entry(' ', cur_color);
    cur_x = 0;
    cur_y = 0;
    update_cursor();
}

void console_scroll(void) {
    if (cur_y < VGA_HEIGHT) return;
    memcpy(vga, vga + VGA_WIDTH, VGA_WIDTH * (VGA_HEIGHT - 1) * 2);
    for (int i = 0; i < VGA_WIDTH; i++)
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = vga_entry(' ', cur_color);
    cur_y = VGA_HEIGHT - 1;
}

void console_newline(void) {
    cur_x = 0;
    cur_y++;
    console_scroll();
    update_cursor();
}

void console_backspace(void) {
    if (cur_x > 0) {
        cur_x--;
    } else if (cur_y > 0) {
        cur_y--;
        cur_x = VGA_WIDTH - 1;
    }
    vga[cur_y * VGA_WIDTH + cur_x] = vga_entry(' ', cur_color);
    update_cursor();
}

void console_putchar(char c) {
    if (c == '\n') { console_newline(); return; }
    if (c == '\r') { cur_x = 0; update_cursor(); return; }
    if (c == '\t') { cur_x = (cur_x + 8) & ~7; if (cur_x >= VGA_WIDTH) console_newline(); update_cursor(); return; }
    if (c == '\b') { console_backspace(); return; }
    vga[cur_y * VGA_WIDTH + cur_x] = vga_entry(c, cur_color);
    cur_x++;
    if (cur_x >= VGA_WIDTH) console_newline();
    else update_cursor();
}

void console_write(const char *s) {
    while (*s) console_putchar(*s++);
}

void console_write_color(const char *s, uint8_t color) {
    uint8_t old = cur_color;
    cur_color = color;
    console_write(s);
    cur_color = old;
}

void console_write_dec(uint32_t n) {
    char buf[12];
    utoa(n, buf, 10);
    console_write(buf);
}

void console_write_hex(uint32_t n) {
    char buf[12];
    console_write("0x");
    utoa(n, buf, 16);
    console_write(buf);
}

void console_set_color(uint8_t fg, uint8_t bg) {
    cur_color = (bg << 4) | (fg & 0x0F);
}

void console_set_cursor(int x, int y) {
    cur_x = x;
    cur_y = y;
    update_cursor();
}

int console_get_x(void) { return cur_x; }
int console_get_y(void) { return cur_y; }
