#include "gui.h"
#include "framebuffer.h"
#include "string.h"
#include "keyboard.h"
#include "../drivers/mouse.h"

uint32_t timer_get_ticks(void);

static gui_window_t windows[MAX_WINDOWS];
static int win_count = 0;
static int win_order[MAX_WINDOWS];
static int order_count = 0;

/* cursor backup */
#define CURSOR_W 12
#define CURSOR_H 16
static uint32_t cursor_save[CURSOR_W * CURSOR_H];
static int cursor_saved_x = -1, cursor_saved_y = -1;

/* mouse state */
static int prev_mx, prev_my, prev_mb;
static int mouse_down_win = -1;

/* start menu */
static int menu_open = 0;
#define MENU_W 200
#define MENU_ITEM_H 28
#define MENU_COUNT 6
static const char *menu_labels[MENU_COUNT] = {
    "File Manager", "Text Editor", "Calculator",
    "Network", "Settings", "Terminal"
};
static int cascade_off = 0;

/* cursor bitmap (1 = white, 2 = black outline) */
static const uint8_t cursor_bmp[CURSOR_H][CURSOR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,2,2,2,2,0},
    {2,1,1,2,1,1,2,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0},
    {2,2,0,0,2,1,1,2,0,0,0,0},
    {2,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,2,2,2,0,0,0,0},
};

/* ── helpers ── */

static void int_to_str(uint32_t v, char *buf, int len) {
    int i = len - 1;
    buf[i--] = '\0';
    if (v == 0) { buf[i] = '0'; while (i > 0) buf[--i] = ' '; return; }
    while (v && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    while (i >= 0) buf[i--] = ' ';
}

static void uptime_str(char *buf) {
    uint32_t sec = timer_get_ticks() / 1000;
    uint32_t h = sec / 3600; sec %= 3600;
    uint32_t m = sec / 60;   sec %= 60;
    buf[0] = '0' + (h / 10); buf[1] = '0' + (h % 10);
    buf[2] = ':';
    buf[3] = '0' + (m / 10); buf[4] = '0' + (m % 10);
    buf[5] = ':';
    buf[6] = '0' + (sec / 10); buf[7] = '0' + (sec % 10);
    buf[8] = '\0';
}

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/* ── z-order management ── */

static void raise_window(int id) {
    int pos = -1;
    for (int i = 0; i < order_count; i++)
        if (win_order[i] == id) { pos = i; break; }
    if (pos < 0) return;
    for (int i = pos; i < order_count - 1; i++)
        win_order[i] = win_order[i + 1];
    win_order[order_count - 1] = id;
    for (int i = 0; i < win_count; i++) windows[i].focused = 0;
    windows[id].focused = 1;
}

static void remove_from_order(int id) {
    int pos = -1;
    for (int i = 0; i < order_count; i++)
        if (win_order[i] == id) { pos = i; break; }
    if (pos < 0) return;
    for (int i = pos; i < order_count - 1; i++)
        win_order[i] = win_order[i + 1];
    order_count--;
}

/* ── cursor draw/restore ── */

static void cursor_restore(void) {
    if (cursor_saved_x < 0) return;
    int sw = fb_width(), sh = fb_height();
    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++) {
            int px = cursor_saved_x + dx, py = cursor_saved_y + dy;
            if (px >= 0 && px < sw && py >= 0 && py < sh)
                fb_putpixel(px, py, cursor_save[dy * CURSOR_W + dx]);
        }
    cursor_saved_x = -1;
}

static void cursor_draw(int mx, int my) {
    int sw = fb_width(), sh = fb_height();
    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++) {
            int px = mx + dx, py = my + dy;
            if (px >= 0 && px < sw && py >= 0 && py < sh)
                cursor_save[dy * CURSOR_W + dx] = fb_getpixel(px, py);
            else
                cursor_save[dy * CURSOR_W + dx] = 0;
        }
    cursor_saved_x = mx; cursor_saved_y = my;
    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++) {
            uint8_t v = cursor_bmp[dy][dx];
            if (!v) continue;
            int px = mx + dx, py = my + dy;
            if (px >= 0 && px < sw && py >= 0 && py < sh)
                fb_putpixel(px, py, v == 1 ? 0xFFFFFF : 0x000000);
        }
}

/* ── drawing primitives ── */

static void draw_desktop(void) {
    int sw = fb_width(), sh = fb_height();
    fb_fill_rect(0, 0, sw, sh - TASKBAR_HEIGHT, FB_BG);
    fb_text(8, 4, "DARKNODE OS", FB_ACCENT);
    char clk[16]; uptime_str(clk);
    fb_text(sw - 80, 4, clk, FB_DIM);
}

static void draw_taskbar(void) {
    int sw = fb_width(), sh = fb_height();
    int ty = sh - TASKBAR_HEIGHT;
    fb_fill_rect(0, ty, sw, TASKBAR_HEIGHT, FB_PANEL);
    fb_draw_rect(0, ty, sw, 1, FB_ACCENT);
    fb_fill_rect(4, ty + 4, 40, TASKBAR_HEIGHT - 8, FB_ACCENT);
    fb_text(10, ty + 10, "DN", FB_BG);

    int bx = 52;
    for (int i = 0; i < order_count; i++) {
        int id = win_order[i];
        gui_window_t *w = &windows[id];
        if (!w->visible) continue;
        int tw = (int)strlen(w->title) * 8 + 16;
        if (tw > 140) tw = 140;
        uint32_t bg = w->focused ? FB_ACCENT : FB_PANEL_HI;
        uint32_t fg = w->focused ? FB_BG : FB_TEXT;
        fb_fill_rect(bx, ty + 4, tw, TASKBAR_HEIGHT - 8, bg);
        fb_text(bx + 8, ty + 10, w->title, fg);
        bx += tw + 4;
    }

    char clk[16]; uptime_str(clk);
    fb_text(sw - 80, ty + 10, clk, FB_TEXT);
}

static void draw_window(int id) {
    gui_window_t *w = &windows[id];
    if (!w->visible) return;
    uint32_t border_color = w->focused ? FB_ACCENT : FB_DIM;
    fb_draw_rect(w->x, w->y, w->w, w->h, border_color);
    uint32_t title_bg = w->focused ? FB_PANEL_HI : FB_PANEL;
    fb_fill_rect(w->x + 1, w->y + 1, w->w - 2, TITLEBAR_HEIGHT, title_bg);
    fb_text(w->x + 8, w->y + 6, w->title, FB_TEXT);
    int cx = w->x + w->w - 22, cy = w->y + 4;
    fb_fill_rect(cx, cy, 18, 16, FB_RED);
    fb_glyph(cx + 5, cy + 2, 'X', FB_TEXT);
    int bx = w->x + 1, by = w->y + TITLEBAR_HEIGHT + 1;
    int bw = w->w - 2, bh = w->h - TITLEBAR_HEIGHT - 2;
    fb_fill_rect(bx, by, bw, bh, FB_BODY);
    if (w->draw_content) {
        w->draw_content(id, bx + 4, by + 4, bw - 8, bh - 8);
    } else if (w->text_len > 0) {
        int tx = bx + 4, ty_s = by + 4;
        int max_lines = (bh - 8) / 14;
        int col = 0, total_lines = 0;
        for (int i = 0; i < w->text_len; i++) {
            if (w->textbuf[i] == '\n') { total_lines++; col = 0; }
            else { col++; if (col >= (bw - 8) / 8) { total_lines++; col = 0; } }
        }
        if (col > 0) total_lines++;
        int skip = total_lines > max_lines ? total_lines - max_lines : 0;
        int line = 0; col = 0; int cur_line = 0;
        for (int i = 0; i < w->text_len && line < max_lines; i++) {
            if (w->textbuf[i] == '\n') { cur_line++; if (cur_line > skip) { line++; col = 0; } else col = 0; continue; }
            if (col >= (bw - 8) / 8) { cur_line++; if (cur_line > skip) { line++; col = 0; } else col = 0; }
            if (cur_line > skip && line < max_lines) { fb_glyph(tx + col * 8, ty_s + line * 14, w->textbuf[i], FB_TEXT); col++; }
            else col++;
        }
    }
}

/* ── start menu drawing ── */

static void draw_start_menu(void) {
    if (!menu_open) return;
    int sh = fb_height();
    int mx = mouse_get_x(), my = mouse_get_y();
    int menu_x = 4;
    int menu_h = MENU_COUNT * MENU_ITEM_H + 8;
    int menu_y = sh - TASKBAR_HEIGHT - menu_h;

    fb_fill_rect(menu_x, menu_y, MENU_W, menu_h, FB_PANEL);
    fb_draw_rect(menu_x, menu_y, MENU_W, menu_h, FB_ACCENT);

    for (int i = 0; i < MENU_COUNT; i++) {
        int iy = menu_y + 4 + i * MENU_ITEM_H;
        int hovered = point_in_rect(mx, my, menu_x, iy, MENU_W, MENU_ITEM_H);
        if (hovered) fb_fill_rect(menu_x + 2, iy, MENU_W - 4, MENU_ITEM_H, FB_ACCENT);
        fb_text(menu_x + 12, iy + 8, menu_labels[i], hovered ? FB_BG : FB_TEXT);
    }
}

/* ── content callbacks for app windows ── */

static void draw_sysinfo(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)cw; (void)ch;
    char buf[32]; int y = cy;
    fb_text(cx, y, "Darknode OS v0.1.0", FB_ACCENT); y += 16;
    fb_text(cx, y, "Custom x86 Kernel", FB_TEXT); y += 24;
    fb_text(cx, y, "Memory:  128 MB", FB_TEXT); y += 16;
    fb_text(cx, y, "Timer:   1000 Hz PIT", FB_TEXT); y += 16;
    fb_text(cx, y, "Disk:    ATA PIO", FB_TEXT); y += 16;
    fb_text(cx, y, "NIC:     NE2000/RTL8029", FB_TEXT); y += 16;
    fb_text(cx, y, "Net:     IPv4/ICMP/UDP/DHCP/DNS", FB_TEXT); y += 16;
    fb_text(cx, y, "FS:      ramfs, devfs", FB_TEXT); y += 24;
    fb_text(cx, y, "Uptime:", FB_DIM);
    uptime_str(buf); fb_text(cx + 64, y, buf, FB_TEXT);
}

static void draw_welcome(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)cw; (void)ch;
    int y = cy;
    fb_text(cx, y, "Welcome to Darknode OS", FB_ACCENT); y += 24;
    fb_text(cx, y, "A security operating system built", FB_TEXT); y += 16;
    fb_text(cx, y, "from scratch. No Linux, no borrowed", FB_TEXT); y += 16;
    fb_text(cx, y, "code -- just raw x86 metal.", FB_TEXT); y += 24;
    fb_text(cx, y, "> Custom kernel with scheduler", FB_TEXT); y += 16;
    fb_text(cx, y, "> VFS with ramfs and devfs", FB_TEXT); y += 16;
    fb_text(cx, y, "> Full TCP/IP networking stack", FB_TEXT); y += 16;
    fb_text(cx, y, "> ATA disk and NE2000 NIC drivers", FB_TEXT); y += 16;
    fb_text(cx, y, "> 28-command built-in shell", FB_TEXT); y += 16;
    fb_text(cx, y, "> Graphical desktop environment", FB_TEXT); y += 24;
    fb_text(cx, y, "Press F1 for terminal. Click DN for apps.", FB_DIM);
}

static void draw_network(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* header */
    fb_text(cx, y, "Network Settings", FB_ACCENT); y += 20;

    /* status */
    fb_fill_rect(cx, y, 8, 8, FB_GREEN);
    fb_text(cx + 14, y - 2, "Connected", FB_GREEN); y += 20;

    /* section: connection info */
    fb_fill_rect(cx, y, cw, 1, FB_DIM); y += 6;
    fb_text(cx, y, "Ethernet", FB_ACCENT); y += 18;

    fb_text(cx, y, "IP Address:", FB_DIM);  fb_text(cx + 112, y, "10.0.2.15", FB_TEXT); y += 16;
    fb_text(cx, y, "Subnet:",    FB_DIM);   fb_text(cx + 112, y, "255.255.255.0", FB_TEXT); y += 16;
    fb_text(cx, y, "Gateway:",   FB_DIM);   fb_text(cx + 112, y, "10.0.2.2", FB_TEXT); y += 16;
    fb_text(cx, y, "DNS:",       FB_DIM);   fb_text(cx + 112, y, "10.0.2.3", FB_TEXT); y += 16;
    fb_text(cx, y, "MAC:",       FB_DIM);   fb_text(cx + 112, y, "52:54:00:12:34:56", FB_TEXT); y += 24;

    /* section: available networks */
    fb_fill_rect(cx, y, cw, 1, FB_DIM); y += 6;
    fb_text(cx, y, "Available Networks", FB_ACCENT); y += 20;

    /* network list */
    static const char *net_names[] = {
        "DARKNODE-LAN",
        "HOME-WIFI-5G",
        "Starbucks-Free",
        "FBI_Surveillance_Van",
        "Guest-Network"
    };
    static const char *net_status[] = {
        "Connected", "Secured", "Open", "Secured", "Open"
    };
    static const int net_bars[] = { 4, 3, 2, 2, 1 };

    for (int i = 0; i < 5 && y + 32 < cy + ch; i++) {
        int ey = y;
        /* highlight connected network */
        if (i == 0) {
            fb_draw_rect(cx, ey - 2, cw, 30, FB_ACCENT);
            fb_fill_rect(cx + 1, ey - 1, cw - 2, 28, FB_PANEL);
        } else {
            fb_fill_rect(cx, ey - 2, cw, 30, (i % 2) ? FB_BODY : FB_PANEL);
        }
        /* signal bars */
        int bx = cx + 4;
        for (int b = 0; b < 4; b++) {
            int bh = 4 + b * 4;
            uint32_t bc = (b < net_bars[i]) ? FB_ACCENT : FB_DARKGRAY;
            fb_fill_rect(bx + b * 6, ey + 20 - bh, 4, bh, bc);
        }
        /* name + status */
        fb_text(cx + 32, ey + 2, net_names[i], FB_TEXT);
        uint32_t sc = (i == 0) ? FB_GREEN : FB_DIM;
        fb_text(cx + cw - (int)strlen(net_status[i]) * 8 - 8, ey + 2, net_status[i], sc);
        y += 32;
    }

    y += 8;
    /* refresh button */
    if (y + 24 < cy + ch) {
        fb_fill_rect(cx, y, 80, 22, FB_PANEL_HI);
        fb_draw_rect(cx, y, 80, 22, FB_ACCENT);
        fb_text(cx + 12, y + 5, "Refresh", FB_TEXT);
    }
}

static void draw_filemanager(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* path bar */
    fb_fill_rect(cx, y, cw, 22, FB_PANEL);
    fb_draw_rect(cx, y, cw, 22, FB_DIM);
    fb_text(cx + 6, y + 5, "/ (ramfs)", FB_TEXT);
    y += 28;

    /* column headers */
    fb_text(cx + 4, y, "Name", FB_DIM);
    fb_text(cx + cw - 60, y, "Type", FB_DIM);
    y += 18;
    fb_fill_rect(cx, y, cw, 1, FB_DIM); y += 4;

    /* entries */
    static const char *dirs[] = { "dev", "tmp", "proc", "etc", "home", "var", "boot" };
    static const char *files[] = { "darknode.conf", "hostname", "motd" };

    for (int i = 0; i < 7 && y + 20 < cy + ch; i++) {
        uint32_t bg = (i % 2) ? FB_BODY : FB_PANEL;
        fb_fill_rect(cx, y, cw, 20, bg);
        fb_text(cx + 4, y + 4, "[D]", FB_ACCENT);
        fb_text(cx + 32, y + 4, dirs[i], FB_TEXT);
        fb_text(cx + cw - 60, y + 4, "Dir", FB_DIM);
        y += 20;
    }
    for (int i = 0; i < 3 && y + 20 < cy + ch; i++) {
        uint32_t bg = ((7 + i) % 2) ? FB_BODY : FB_PANEL;
        fb_fill_rect(cx, y, cw, 20, bg);
        fb_text(cx + 4, y + 4, "[F]", FB_MUTED);
        fb_text(cx + 32, y + 4, files[i], FB_TEXT);
        fb_text(cx + cw - 60, y + 4, "File", FB_DIM);
        y += 20;
    }
}

static void draw_calculator(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* display */
    fb_fill_rect(cx, y, cw, 36, FB_PANEL);
    fb_draw_rect(cx, y, cw, 36, FB_DIM);
    fb_text(cx + cw - 24, y + 12, "0", FB_TEXT);
    y += 44;

    /* button grid */
    static const char *rows[] = { "7 8 9 /", "4 5 6 *", "1 2 3 -", "0 . = +" };
    int btn_w = (cw - 12) / 4;
    int btn_h = 32;

    for (int r = 0; r < 4; r++) {
        int bx = cx;
        const char *row = rows[r];
        int col = 0;
        for (int c = 0; row[c]; c++) {
            if (row[c] == ' ') continue;
            char ch2 = row[c];
            uint32_t bg;
            if (ch2 == '=') bg = FB_ACCENT;
            else if (ch2 >= '0' && ch2 <= '9') bg = FB_PANEL_HI;
            else if (ch2 == '.') bg = FB_PANEL_HI;
            else bg = FB_DIM;

            int x0 = bx + col * (btn_w + 4);
            fb_fill_rect(x0, y, btn_w, btn_h, bg);
            fb_draw_rect(x0, y, btn_w, btn_h, FB_MUTED);

            uint32_t fg = (ch2 == '=') ? FB_BG : FB_TEXT;
            fb_glyph(x0 + btn_w / 2 - 4, y + 10, ch2, fg);
            col++;
        }
        y += btn_h + 4;
    }
}

static void draw_settings(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* Display section */
    fb_fill_rect(cx, y, cw, 1, FB_DIM);
    fb_text(cx, y + 4, "Display", FB_ACCENT); y += 24;
    fb_text(cx + 8, y, "Resolution:", FB_DIM);   fb_text(cx + 120, y, "1024x768", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Color depth:", FB_DIM);  fb_text(cx + 120, y, "32-bit BGRA", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Refresh:", FB_DIM);      fb_text(cx + 120, y, "60 Hz", FB_TEXT); y += 24;

    /* System section */
    fb_fill_rect(cx, y, cw, 1, FB_DIM);
    fb_text(cx, y + 4, "System", FB_ACCENT); y += 24;
    fb_text(cx + 8, y, "Kernel:", FB_DIM);    fb_text(cx + 120, y, "Darknode OS v0.1.0", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Arch:", FB_DIM);      fb_text(cx + 120, y, "x86 (i386)", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Scheduler:", FB_DIM); fb_text(cx + 120, y, "Round-robin", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Syscalls:", FB_DIM);  fb_text(cx + 120, y, "INT 0x80 (11 handlers)", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Heap:", FB_DIM);      fb_text(cx + 120, y, "4 MiB at 0x400000", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Timer:", FB_DIM);     fb_text(cx + 120, y, "PIT 1000 Hz", FB_TEXT); y += 24;

    /* About section */
    fb_fill_rect(cx, y, cw, 1, FB_DIM);
    fb_text(cx, y + 4, "About", FB_ACCENT); y += 24;
    fb_text(cx + 8, y, "Darknode OS", FB_TEXT); y += 16;
    fb_text(cx + 8, y, "Custom x86 kernel built from scratch.", FB_DIM); y += 16;
    fb_text(cx + 8, y, "No Linux. No borrowed code.", FB_DIM); y += 20;
    fb_text(cx + 8, y, "github.com/cashzombs-stack/darknode-os", FB_ACCENT);
}

static void draw_texteditor(int wid, int cx, int cy, int cw, int ch) {
    (void)wid;
    /* toolbar */
    fb_fill_rect(cx, cy, cw, 22, FB_PANEL);
    fb_text(cx + 4, cy + 5, "File  Edit  View", FB_DIM);
    fb_fill_rect(cx, cy + 22, cw, 1, FB_DIM);

    /* text area */
    int ty = cy + 28;
    fb_text(cx + 4, ty, "untitled.txt", FB_MUTED); ty += 18;
    fb_fill_rect(cx, ty, cw, 1, FB_DIM); ty += 6;

    /* line numbers + content area */
    fb_fill_rect(cx, ty, 32, ch - 34, FB_PANEL);
    for (int i = 0; i < (ch - 40) / 16 && i < 30; i++) {
        char ln[4];
        ln[0] = '0' + ((i + 1) / 10);
        ln[1] = '0' + ((i + 1) % 10);
        ln[2] = '\0';
        fb_text(cx + 8, ty + i * 16, ln, FB_DIM);
    }

    /* cursor blink */
    int blink = (timer_get_ticks() / 500) % 2;
    fb_text(cx + 38, ty, "Type here...", FB_DIM);
    if (blink) fb_glyph(cx + 38 + 12 * 8, ty, '_', FB_ACCENT);
}

/* ── public API ── */

void gui_redraw(void) {
    cursor_restore();
    draw_desktop();
    for (int i = 0; i < order_count; i++)
        draw_window(win_order[i]);
    draw_taskbar();
    draw_start_menu();
    cursor_draw(mouse_get_x(), mouse_get_y());
}

int gui_create_window(const char* title, int x, int y, int w, int h,
                       void (*draw_fn)(int, int, int, int, int)) {
    if (win_count >= MAX_WINDOWS) return -1;
    int id = win_count++;
    gui_window_t *win = &windows[id];
    memset(win, 0, sizeof(gui_window_t));
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->visible = 1; win->focused = 0;
    win->draw_content = draw_fn;
    win->text_len = 0; win->scroll = 0;
    int tlen = (int)strlen(title);
    if (tlen > 63) tlen = 63;
    memcpy(win->title, title, tlen);
    win->title[tlen] = '\0';
    win_order[order_count++] = id;
    raise_window(id);
    return id;
}

void gui_close_window(int id) {
    if (id < 0 || id >= win_count) return;
    windows[id].visible = 0;
    remove_from_order(id);
}

void gui_window_print(int id, const char* text) {
    if (id < 0 || id >= win_count) return;
    gui_window_t *w = &windows[id];
    int len = (int)strlen(text);
    for (int i = 0; i < len && w->text_len < 4095; i++)
        w->textbuf[w->text_len++] = text[i];
    w->textbuf[w->text_len] = '\0';
}

/* ── start menu app launcher ── */

static void launch_app(int index) {
    int ox = 100 + cascade_off;
    int oy = 60 + cascade_off;
    cascade_off = (cascade_off + 30) % 180;

    switch (index) {
    case 0: gui_create_window("File Manager", ox, oy, 420, 380, draw_filemanager); break;
    case 1: gui_create_window("Text Editor", ox, oy, 500, 380, draw_texteditor); break;
    case 2: gui_create_window("Calculator", ox, oy, 240, 280, draw_calculator); break;
    case 3: gui_create_window("Network", ox, oy, 420, 440, draw_network); break;
    case 4: gui_create_window("Settings", ox, oy, 440, 420, draw_settings); break;
    case 5: {
        int tid = gui_create_window("Terminal", ox, oy, 500, 320, 0);
        gui_window_print(tid, "darknode> _\n");
        break;
    }
    }
}

/* ── event handling ── */

static int hit_test_window(int mx, int my, int *part) {
    for (int i = order_count - 1; i >= 0; i--) {
        int id = win_order[i];
        gui_window_t *w = &windows[id];
        if (!w->visible) continue;
        if (!point_in_rect(mx, my, w->x, w->y, w->w, w->h)) continue;
        if (point_in_rect(mx, my, w->x + w->w - 22, w->y + 4, 18, 16)) { *part = 2; return id; }
        if (my < w->y + TITLEBAR_HEIGHT) { *part = 1; return id; }
        *part = 0; return id;
    }
    return -1;
}

static int hit_test_taskbar_window(int mx) {
    int bx = 52;
    for (int i = 0; i < order_count; i++) {
        int id = win_order[i];
        gui_window_t *w = &windows[id];
        if (!w->visible) continue;
        int tw = (int)strlen(w->title) * 8 + 16;
        if (tw > 140) tw = 140;
        if (mx >= bx && mx < bx + tw) return id;
        bx += tw + 4;
    }
    return -1;
}

static void handle_mouse(void) {
    int mx = mouse_get_x(), my = mouse_get_y(), mb = mouse_get_buttons();
    int sh = fb_height();
    int pressed = (mb & 1) && !(prev_mb & 1);
    int released = !(mb & 1) && (prev_mb & 1);
    int held = (mb & 1);

    if (pressed) {
        /* check start menu click first */
        if (menu_open) {
            int menu_h = MENU_COUNT * MENU_ITEM_H + 8;
            int menu_y = sh - TASKBAR_HEIGHT - menu_h;
            if (point_in_rect(mx, my, 4, menu_y, MENU_W, menu_h)) {
                int idx = (my - menu_y - 4) / MENU_ITEM_H;
                if (idx >= 0 && idx < MENU_COUNT) {
                    launch_app(idx);
                    menu_open = 0;
                }
            } else {
                menu_open = 0;
            }
            goto done;
        }

        if (my >= sh - TASKBAR_HEIGHT) {
            if (mx < 48) {
                menu_open = !menu_open;
            } else {
                int id = hit_test_taskbar_window(mx);
                if (id >= 0) raise_window(id);
            }
        } else {
            int part;
            int id = hit_test_window(mx, my, &part);
            if (id >= 0) {
                raise_window(id);
                if (part == 2) gui_close_window(id);
                else if (part == 1) {
                    windows[id].dragging = 1;
                    windows[id].drag_ox = mx - windows[id].x;
                    windows[id].drag_oy = my - windows[id].y;
                    mouse_down_win = id;
                }
            }
        }
    }

    if (held && mouse_down_win >= 0) {
        gui_window_t *w = &windows[mouse_down_win];
        if (w->dragging) {
            w->x = mx - w->drag_ox;
            w->y = my - w->drag_oy;
            if (w->x < 0) w->x = 0;
            if (w->y < 0) w->y = 0;
            int sw = fb_width();
            if (w->x + w->w > sw) w->x = sw - w->w;
            if (w->y + w->h > sh - TASKBAR_HEIGHT) w->y = sh - TASKBAR_HEIGHT - w->h;
        }
    }

    if (released) {
        if (mouse_down_win >= 0) {
            windows[mouse_down_win].dragging = 0;
            mouse_down_win = -1;
        }
    }

done:
    prev_mx = mx; prev_my = my; prev_mb = mb;
}

/* ── init & main loop ── */

void gui_init(void) {
    memset(windows, 0, sizeof(windows));
    win_count = 0; order_count = 0;
    mouse_down_win = -1; cursor_saved_x = -1;
    prev_mx = prev_my = prev_mb = 0;
    menu_open = 0; cascade_off = 0;

    fb_clear(FB_BG);

    gui_create_window("Welcome", 200, 80, 380, 340, draw_welcome);
    gui_create_window("System Info", 60, 60, 340, 280, draw_sysinfo);

    int term_id = gui_create_window("Terminal", 320, 160, 500, 320, 0);
    gui_window_print(term_id,
        "Darknode OS v0.1.0 booting...\n"
        "[ok] GDT loaded\n"
        "[ok] IDT loaded, interrupts enabled\n"
        "[ok] PIT timer at 1000 Hz\n"
        "[ok] PS/2 keyboard initialized\n"
        "[ok] Physical memory manager ready\n"
        "[ok] Heap allocator ready\n"
        "[ok] VFS mounted (ramfs at /)\n"
        "[ok] devfs mounted at /dev\n"
        "[ok] ATA disk driver loaded\n"
        "[ok] PCI bus scanned\n"
        "[ok] NE2000 NIC detected\n"
        "[ok] TCP/IP stack initialized\n"
        "[ok] PS/2 mouse initialized\n"
        "[ok] Framebuffer 1024x768x32\n"
        "[ok] GUI desktop started\n"
        "\n"
        "darknode> _\n"
    );

    gui_redraw();
}

void gui_run(void) {
    uint32_t last_redraw = 0;
    while (1) {
        handle_mouse();
        char key = keyboard_getchar();
        if (key == 0x3B) {
            int id = gui_create_window("Terminal",
                80 + (win_count * 20) % 200, 60 + (win_count * 20) % 150,
                500, 320, 0);
            gui_window_print(id, "darknode> _\n");
        }
        if (timer_get_ticks() - last_redraw >= 33) {
            gui_redraw();
            last_redraw = timer_get_ticks();
        }
    }
}
