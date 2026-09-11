#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include "../include/types.h"

void fb_init(uint32_t* addr, int width, int height, int pitch);
void fb_putpixel(int x, int y, uint32_t color);
uint32_t fb_getpixel(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char* str, uint32_t fg, uint32_t bg);
void fb_clear(uint32_t color);
void fb_scroll_region(int x, int y, int w, int h, int lines, uint32_t fill);
int fb_width(void);
int fb_height(void);
void fb_copy_rect(int dx, int dy, int sx, int sy, int w, int h);

#define FB_CHAR_W 8
#define FB_CHAR_H 16

#define FB_BLACK    0x00000000
#define FB_WHITE    0x00FFFFFF
#define FB_RED      0x000000FF
#define FB_GREEN    0x0000FF00
#define FB_BLUE     0x00FF0000
#define FB_CYAN     0x00FFFF00
#define FB_YELLOW   0x0000FFFF
#define FB_GRAY     0x00808080
#define FB_DARKGRAY 0x00404040
#define FB_LIGHTGRAY 0x00C0C0C0

#define FB_BG       0x00181C20
#define FB_PANEL    0x00252A30
#define FB_PANEL_HI 0x00353A40
#define FB_ACCENT   0x00C8DC00
#define FB_TEXT     0x00E0E8F0
#define FB_MUTED    0x00808890
#define FB_DIM      0x00606870
#define FB_BODY     0x001E2228
#define FB_TRANSPARENT 0x01000000

static inline void fb_text(int x, int y, const char* s, uint32_t fg) {
    fb_draw_string(x, y, s, fg, FB_TRANSPARENT);
}
static inline void fb_glyph(int x, int y, char c, uint32_t fg) {
    fb_draw_char(x, y, c, fg, FB_TRANSPARENT);
}

#endif
