#include "gui.h"
#include "theme.h"
#include "framebuffer.h"
#include "string.h"
#include "keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/usb_hid.h"
#include "../drivers/vbox.h"

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
#define MENU_COUNT 12
static const char *menu_labels[MENU_COUNT] = {
    "Terminal",      "File Manager",  "Text Editor",
    "Web Browser",   "Task Manager",  "Disk Manager",
    "Calculator",    "Network",       "Firewall",
    "Hash Tools",    "Settings",      "About Darknode"
};
static int cascade_off = 0;
static int minimalist_mode = 1;
static int settings_tab = 0;

/* calculator state */
static int calc_val = 0;
static int calc_prev = 0;
static char calc_op = 0;
static int calc_new = 1;

/* right-click context menu */
static int rclick_open = 0;
static int rclick_x = 0, rclick_y = 0;
static int rclick_type = 0; /* 0=desktop, 1=window title */
static int rclick_win = -1;
#define RCLICK_W 140
#define RCLICK_ITEM_H 24

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

static void __attribute__((unused)) int_to_str(uint32_t v, char *buf, int len) {
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

/* ── desktop icons ── */
#define ICON_W 72
#define ICON_H 56
#define ICON_COLS 1
#define ICON_COUNT 6
static const char *icon_labels[ICON_COUNT] = {
    "Terminal", "Files", "Browser", "Network", "Settings", "About"
};
static const int icon_app_map[ICON_COUNT] = { 0, 1, 3, 7, 10, 11 };
static const char icon_glyphs[ICON_COUNT] = { '>', 'F', 'W', 'N', 'S', '?' };

static int tb_h(void) { return minimalist_mode ? 32 : 38; }
static int title_h(void) { return minimalist_mode ? 24 : 28; }
static int border_w(void) { return minimalist_mode ? 1 : 2; }

static void draw_desktop(void) {
    int sw = fb_width(), sh = fb_height();
    fb_fill_rect(0, 0, sw, sh - tb_h(), T_BG);

    /* top bar — subtle, like a panel */
    fb_fill_rect(0, 0, sw, 22, T_PANEL);
    fb_text(8, 5, "Darknode OS", T_ACCENT);
    char clk[16]; uptime_str(clk);
    /* status indicators on top bar */
    fb_fill_rect(sw - 180, 7, 6, 6, T_OK); /* network dot */
    fb_text(sw - 170, 5, "ETH", T_DIM);
    fb_text(sw - 130, 5, "128M", T_DIM);
    fb_text(sw - 80, 5, clk, T_TEXT);

    /* desktop icons — left side, vertical column */
    int ix = 16;
    int iy = 40;
    for (int i = 0; i < ICON_COUNT && iy + ICON_H < sh - tb_h() - 10; i++) {
        /* icon box */
        fb_fill_rect(ix + 16, iy, 40, 32, T_PANEL_HI);
        fb_draw_rect(ix + 16, iy, 40, 32, T_DIM);
        fb_glyph(ix + 32, iy + 10, icon_glyphs[i], T_ACCENT);
        /* label centered below */
        int lw = (int)strlen(icon_labels[i]) * 8;
        fb_text(ix + (ICON_W - lw) / 2, iy + 36, icon_labels[i], T_TEXT);
        iy += ICON_H + 8;
    }
}

static void draw_taskbar(void) {
    int sw = fb_width(), sh = fb_height();
    int th = tb_h();
    int ty = sh - th;
    fb_fill_rect(0, ty, sw, th, T_PANEL);
    fb_draw_rect(0, ty, sw, 1, T_ACCENT);

    int btn_h = th - 8;
    int btn_y = ty + 4;
    int text_y = ty + (th - 16) / 2;

    /* DN button */
    fb_fill_rect(4, btn_y, 40, btn_h, T_ACCENT);
    if (!minimalist_mode) {
        fb_draw_line(4, btn_y, 44, btn_y, T_TEXT); /* top highlight */
        fb_draw_line(4, btn_y + btn_h - 1, 44, btn_y + btn_h - 1, T_DIM); /* bottom shadow */
    }
    fb_text(10, text_y, "DN", T_BG);

    int bx = 52;
    for (int i = 0; i < order_count; i++) {
        int id = win_order[i];
        gui_window_t *w = &windows[id];
        int tw = (int)strlen(w->title) * 8 + 16;
        if (tw > 140) tw = 140;
        uint32_t bg, fg;
        if (!w->visible) {
            bg = T_PANEL; fg = T_DIM; /* minimized — dimmed */
        } else if (w->focused) {
            bg = T_ACCENT; fg = T_BG;
        } else {
            bg = T_PANEL_HI; fg = T_TEXT;
        }
        fb_fill_rect(bx, btn_y, tw, btn_h, bg);
        if (!minimalist_mode) {
            fb_draw_line(bx, btn_y, bx + tw, btn_y, w->focused ? T_TEXT : T_MUTED);
            fb_draw_line(bx, btn_y + btn_h - 1, bx + tw, btn_y + btn_h - 1, T_DIM);
        }
        fb_text(bx + 8, text_y, w->title, fg);
        bx += tw + 4;
    }

    char clk[16]; uptime_str(clk);
    fb_text(sw - 80, text_y, clk, T_TEXT);
}

static void draw_window(int id) {
    gui_window_t *w = &windows[id];
    if (!w->visible) return;
    int th = title_h();
    int brd = border_w();
    uint32_t bcol = w->focused ? T_ACCENT : T_DIM;

    if (minimalist_mode) {
        fb_draw_rect(w->x, w->y, w->w, w->h, bcol);
    } else {
        fb_draw_rect(w->x, w->y, w->w, w->h, bcol);
        fb_draw_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, T_PANEL_HI);
    }

    uint32_t title_bg = w->focused ? T_PANEL_HI : T_PANEL;
    fb_fill_rect(w->x + brd, w->y + brd, w->w - brd * 2, th, title_bg);

    if (minimalist_mode) {
        fb_text(w->x + 8, w->y + (th - 16) / 2 + brd, w->title, T_TEXT);
    } else {
        int tw = (int)strlen(w->title) * 8;
        fb_text(w->x + (w->w - tw) / 2, w->y + (th - 16) / 2 + brd, w->title, T_TEXT);
    }

    /* Window buttons: [_] minimize, [O] maximize, [X] close */
    int btn_sz = minimalist_mode ? 16 : 18;
    int btn_y = w->y + brd + (th - btn_sz) / 2;
    int btn_gap = btn_sz + 4;

    /* Minimize */
    int min_x = w->x + w->w - btn_gap * 3 - 2;
    fb_fill_rect(min_x, btn_y, btn_sz, btn_sz, T_PANEL);
    fb_draw_rect(min_x, btn_y, btn_sz, btn_sz, T_DIM);
    fb_fill_rect(min_x + 3, btn_y + btn_sz - 5, btn_sz - 6, 2, T_TEXT);

    /* Maximize */
    int max_x = w->x + w->w - btn_gap * 2 - 2;
    fb_fill_rect(max_x, btn_y, btn_sz, btn_sz, T_PANEL);
    fb_draw_rect(max_x, btn_y, btn_sz, btn_sz, T_DIM);
    fb_draw_rect(max_x + 3, btn_y + 3, btn_sz - 6, btn_sz - 6, T_TEXT);
    fb_fill_rect(max_x + 3, btn_y + 3, btn_sz - 6, 2, T_TEXT);

    /* Close */
    int cx = w->x + w->w - btn_gap - 2;
    fb_fill_rect(cx, btn_y, btn_sz, btn_sz, T_ERR);
    if (!minimalist_mode) fb_draw_rect(cx, btn_y, btn_sz, btn_sz, T_PANEL_HI);
    fb_glyph(cx + (btn_sz - 8) / 2, btn_y + (btn_sz - 16) / 2, 'X', T_TEXT);

    /* Body */
    int bx = w->x + brd, by = w->y + th + brd;
    int bw = w->w - brd * 2, bh = w->h - th - brd * 2;
    fb_fill_rect(bx, by, bw, bh, T_BODY);

    /* Resize grip — bottom right corner */
    int gx = w->x + w->w - 12, gy = w->y + w->h - 12;
    for (int i = 0; i < 3; i++) {
        fb_fill_rect(gx + i * 4, gy + 8 - i * 4, 2, 2 + i * 4, T_DIM);
    }
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
            if (cur_line > skip && line < max_lines) { fb_glyph(tx + col * 8, ty_s + line * 14, w->textbuf[i], T_TEXT); col++; }
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
    int menu_y = sh - tb_h() - menu_h;

    fb_fill_rect(menu_x, menu_y, MENU_W, menu_h, T_PANEL);
    fb_draw_rect(menu_x, menu_y, MENU_W, menu_h, T_ACCENT);

    for (int i = 0; i < MENU_COUNT; i++) {
        int iy = menu_y + 4 + i * MENU_ITEM_H;
        int hovered = point_in_rect(mx, my, menu_x, iy, MENU_W, MENU_ITEM_H);
        if (hovered) fb_fill_rect(menu_x + 2, iy, MENU_W - 4, MENU_ITEM_H, T_ACCENT);
        fb_text(menu_x + 12, iy + 8, menu_labels[i], hovered ? T_BG : T_TEXT);
    }
}

/* ── content callbacks for app windows ── */

static void __attribute__((unused)) draw_sysinfo(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)cw; (void)ch;
    char buf[32]; int y = cy;
    fb_text(cx, y, "Darknode OS v0.1.0", T_ACCENT); y += 16;
    fb_text(cx, y, "Custom x86 Kernel", T_TEXT); y += 24;
    fb_text(cx, y, "Memory:  128 MB", T_TEXT); y += 16;
    fb_text(cx, y, "Timer:   1000 Hz PIT", T_TEXT); y += 16;
    fb_text(cx, y, "Disk:    ATA PIO", T_TEXT); y += 16;
    fb_text(cx, y, "NIC:     NE2000/RTL8029", T_TEXT); y += 16;
    fb_text(cx, y, "Net:     IPv4/ICMP/UDP/DHCP/DNS", T_TEXT); y += 16;
    fb_text(cx, y, "FS:      ramfs, devfs", T_TEXT); y += 24;
    fb_text(cx, y, "Uptime:", T_DIM);
    uptime_str(buf); fb_text(cx + 64, y, buf, T_TEXT);
}

static void draw_welcome(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)cw; (void)ch;
    int y = cy;
    fb_text(cx, y, "Welcome to Darknode OS", T_ACCENT); y += 24;
    fb_text(cx, y, "A security operating system built", T_TEXT); y += 16;
    fb_text(cx, y, "from scratch. No Linux, no borrowed", T_TEXT); y += 16;
    fb_text(cx, y, "code -- just raw x86 metal.", T_TEXT); y += 24;
    fb_text(cx, y, "> Custom kernel with scheduler", T_TEXT); y += 16;
    fb_text(cx, y, "> VFS with ramfs and devfs", T_TEXT); y += 16;
    fb_text(cx, y, "> Full TCP/IP networking stack", T_TEXT); y += 16;
    fb_text(cx, y, "> ATA disk and NE2000 NIC drivers", T_TEXT); y += 16;
    fb_text(cx, y, "> 28-command built-in shell", T_TEXT); y += 16;
    fb_text(cx, y, "> Graphical desktop environment", T_TEXT); y += 24;
    fb_text(cx, y, "Press F1 for terminal. Click DN for apps.", T_DIM);
}

static void draw_network(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* header */
    fb_text(cx, y, "Network Settings", T_ACCENT); y += 20;

    /* status */
    fb_fill_rect(cx, y, 8, 8, T_OK);
    fb_text(cx + 14, y - 2, "Connected", T_OK); y += 20;

    /* section: connection info */
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 6;
    fb_text(cx, y, "Ethernet", T_ACCENT); y += 18;

    fb_text(cx, y, "IP Address:", T_DIM);  fb_text(cx + 112, y, "10.0.2.15", T_TEXT); y += 16;
    fb_text(cx, y, "Subnet:",    T_DIM);   fb_text(cx + 112, y, "255.255.255.0", T_TEXT); y += 16;
    fb_text(cx, y, "Gateway:",   T_DIM);   fb_text(cx + 112, y, "10.0.2.2", T_TEXT); y += 16;
    fb_text(cx, y, "DNS:",       T_DIM);   fb_text(cx + 112, y, "10.0.2.3", T_TEXT); y += 16;
    fb_text(cx, y, "MAC:",       T_DIM);   fb_text(cx + 112, y, "52:54:00:12:34:56", T_TEXT); y += 24;

    /* section: available networks */
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 6;
    fb_text(cx, y, "Available Networks", T_ACCENT); y += 20;

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
            fb_draw_rect(cx, ey - 2, cw, 30, T_ACCENT);
            fb_fill_rect(cx + 1, ey - 1, cw - 2, 28, T_PANEL);
        } else {
            fb_fill_rect(cx, ey - 2, cw, 30, (i % 2) ? T_BODY : T_PANEL);
        }
        /* signal bars */
        int bx = cx + 4;
        for (int b = 0; b < 4; b++) {
            int bh = 4 + b * 4;
            uint32_t bc = (b < net_bars[i]) ? T_ACCENT : FB_DARKGRAY;
            fb_fill_rect(bx + b * 6, ey + 20 - bh, 4, bh, bc);
        }
        /* name + status */
        fb_text(cx + 32, ey + 2, net_names[i], T_TEXT);
        uint32_t sc = (i == 0) ? T_OK : T_DIM;
        fb_text(cx + cw - (int)strlen(net_status[i]) * 8 - 8, ey + 2, net_status[i], sc);
        y += 32;
    }

    y += 8;
    /* refresh button */
    if (y + 24 < cy + ch) {
        fb_fill_rect(cx, y, 80, 22, T_PANEL_HI);
        fb_draw_rect(cx, y, 80, 22, T_ACCENT);
        fb_text(cx + 12, y + 5, "Refresh", T_TEXT);
    }
}

static void draw_filemanager(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* path bar */
    fb_fill_rect(cx, y, cw, 22, T_PANEL);
    fb_draw_rect(cx, y, cw, 22, T_DIM);
    fb_text(cx + 6, y + 5, "/ (ramfs)", T_TEXT);
    y += 28;

    /* column headers */
    fb_text(cx + 4, y, "Name", T_DIM);
    fb_text(cx + cw - 60, y, "Type", T_DIM);
    y += 18;
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 4;

    /* entries */
    static const char *dirs[] = { "dev", "tmp", "proc", "etc", "home", "var", "boot" };
    static const char *files[] = { "darknode.conf", "hostname", "motd" };

    for (int i = 0; i < 7 && y + 20 < cy + ch; i++) {
        uint32_t bg = (i % 2) ? T_BODY : T_PANEL;
        fb_fill_rect(cx, y, cw, 20, bg);
        fb_text(cx + 4, y + 4, "[D]", T_ACCENT);
        fb_text(cx + 32, y + 4, dirs[i], T_TEXT);
        fb_text(cx + cw - 60, y + 4, "Dir", T_DIM);
        y += 20;
    }
    for (int i = 0; i < 3 && y + 20 < cy + ch; i++) {
        uint32_t bg = ((7 + i) % 2) ? T_BODY : T_PANEL;
        fb_fill_rect(cx, y, cw, 20, bg);
        fb_text(cx + 4, y + 4, "[F]", T_MUTED);
        fb_text(cx + 32, y + 4, files[i], T_TEXT);
        fb_text(cx + cw - 60, y + 4, "File", T_DIM);
        y += 20;
    }
}

static void calc_digit(int d) {
    if (calc_new) { calc_val = 0; calc_new = 0; }
    if (calc_val < 99999999) calc_val = calc_val * 10 + d;
}

static void calc_operator(char op) {
    if (calc_op && !calc_new) {
        switch (calc_op) {
        case '+': calc_prev = calc_prev + calc_val; break;
        case '-': calc_prev = calc_prev - calc_val; break;
        case '*': calc_prev = calc_prev * calc_val; break;
        case '/': if (calc_val != 0) calc_prev = calc_prev / calc_val; break;
        }
    } else {
        calc_prev = calc_val;
    }
    calc_op = op;
    calc_new = 1;
}

static void calc_equals(void) {
    if (calc_op) {
        switch (calc_op) {
        case '+': calc_val = calc_prev + calc_val; break;
        case '-': calc_val = calc_prev - calc_val; break;
        case '*': calc_val = calc_prev * calc_val; break;
        case '/': if (calc_val != 0) calc_val = calc_prev / calc_val; break;
        }
    }
    calc_op = 0;
    calc_prev = 0;
    calc_new = 1;
}

static void calc_int_to_str(int v, char *buf) {
    if (v < 0) { buf[0] = '-'; calc_int_to_str(-v, buf + 1); return; }
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[16]; int i = 0;
    while (v > 0 && i < 15) { tmp[i++] = '0' + (v % 10); v /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

static void draw_calculator(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;

    /* display */
    fb_fill_rect(cx, y, cw, 36, T_PANEL);
    fb_draw_rect(cx, y, cw, 36, T_DIM);
    char dispbuf[16];
    calc_int_to_str(calc_val, dispbuf);
    int dlen = (int)strlen(dispbuf);
    fb_text(cx + cw - dlen * 8 - 8, y + 12, dispbuf, T_TEXT);
    if (calc_op) {
        char opbuf[2] = { calc_op, '\0' };
        fb_text(cx + 8, y + 12, opbuf, T_DIM);
    }
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
            if (ch2 == '=') bg = T_ACCENT;
            else if (ch2 >= '0' && ch2 <= '9') bg = T_PANEL_HI;
            else if (ch2 == '.') bg = T_PANEL_HI;
            else bg = T_DIM;

            int x0 = bx + col * (btn_w + 4);
            fb_fill_rect(x0, y, btn_w, btn_h, bg);
            fb_draw_rect(x0, y, btn_w, btn_h, T_MUTED);

            uint32_t fg = (ch2 == '=') ? T_BG : T_TEXT;
            fb_glyph(x0 + btn_w / 2 - 4, y + 10, ch2, fg);
            col++;
        }
        y += btn_h + 4;
    }
}

static void draw_toggle(int x, int y, int on) {
    /* Toggle switch: 36x16 */
    uint32_t track = on ? T_ACCENT : T_DIM;
    fb_fill_rect(x, y, 36, 16, track);
    fb_draw_rect(x, y, 36, 16, T_DIM);
    int knob_x = on ? x + 20 : x + 2;
    fb_fill_rect(knob_x, y + 2, 14, 12, on ? T_TEXT : T_MUTED);
}

static void draw_setting_row(int x, int y, int w, const char *label, const char *value) {
    fb_text(x, y, label, T_MUTED);
    int vx = x + 130;
    int max_chars = (w - 138) / 8;
    if (max_chars < 1) max_chars = 1;
    /* Truncate value if it doesn't fit */
    char vbuf[64];
    int vlen = (int)strlen(value);
    if (vlen > max_chars && max_chars < 63) {
        for (int i = 0; i < max_chars - 2 && i < 61; i++) vbuf[i] = value[i];
        vbuf[max_chars - 2] = '.'; vbuf[max_chars - 1] = '.'; vbuf[max_chars] = '\0';
        fb_text(vx, y, vbuf, T_TEXT);
    } else {
        fb_text(vx, y, value, T_TEXT);
    }
}

static void draw_slider(int x, int y, int w, int pct) {
    /* Track */
    fb_fill_rect(x, y + 2, w, 6, T_DIM);
    /* Filled portion */
    int fill = (w * pct) / 100;
    if (fill > 0) fb_fill_rect(x, y + 2, fill, 6, T_ACCENT);
    /* Knob */
    int kx = x + fill - 5;
    if (kx < x) kx = x;
    fb_fill_rect(kx, y, 10, 10, T_TEXT);
    fb_draw_rect(kx, y, 10, 10, T_ACCENT);
    /* Label */
    char pstr[5];
    pstr[0] = '0' + (pct / 100) % 10;
    pstr[1] = '0' + (pct / 10) % 10;
    pstr[2] = '0' + pct % 10;
    pstr[3] = '%';
    pstr[4] = '\0';
    if (pct < 100) { pstr[0] = pstr[1]; pstr[1] = pstr[2]; pstr[2] = '%'; pstr[3] = '\0'; }
    if (pct < 10) { pstr[0] = pstr[1]; pstr[1] = '%'; pstr[2] = '\0'; }
    fb_text(x + w + 8, y, pstr, T_MUTED);
}

static void draw_settings(int wid, int cx, int cy, int cw, int ch) {
    (void)wid;
    int mx = mouse_get_x(), my = mouse_get_y();

    /* Sidebar — left panel with tabs */
    int sidebar_w = 110;
    fb_fill_rect(cx, cy, sidebar_w, ch, T_PANEL);
    fb_draw_line(cx + sidebar_w, cy, cx + sidebar_w, cy + ch, T_DIM);

    static const char *tabs[] = {
        "Appearance", "Display", "Network", "Sound",
        "Power", "Storage", "Keyboard", "Mouse",
        "Privacy", "Updates", "System", "About"
    };
    int tab_count = 12;
    for (int i = 0; i < tab_count; i++) {
        int ty = cy + 8 + i * 26;
        int hovered = point_in_rect(mx, my, cx, ty, sidebar_w, 24);
        if (i == settings_tab) {
            fb_fill_rect(cx, ty, sidebar_w, 24, T_ACCENT);
            fb_text(cx + 10, ty + 6, tabs[i], T_BG);
        } else if (hovered) {
            fb_fill_rect(cx, ty, sidebar_w, 24, T_PANEL_HI);
            fb_text(cx + 10, ty + 6, tabs[i], T_TEXT);
        } else {
            fb_text(cx + 10, ty + 6, tabs[i], T_MUTED);
        }
    }

    /* Content area */
    int px = cx + sidebar_w + 16;
    int pw = cw - sidebar_w - 32;
    int y = cy + 8;

    switch (settings_tab) {
    case 0: /* Appearance */
        fb_text(px, y, "Appearance", T_ACCENT); y += 28;

        /* Minimalist mode toggle */
        fb_fill_rect(px, y, pw, 40, T_PANEL);
        fb_text(px + 10, y + 6, "Minimalist Mode", T_TEXT);
        fb_text(px + 10, y + 22, minimalist_mode ? "Sharp corners, thin borders, compact" : "Distro style: thick borders, padded, XFCE-like", T_DIM);
        draw_toggle(px + pw - 46, y + 12, minimalist_mode);
        y += 48;

        /* Theme picker */
        fb_text(px, y, "Color Theme", T_MUTED); y += 18;
        for (int i = 0; i < theme_count() && y + 26 < cy + ch; i++) {
            int active = (i == theme_get());
            int hovered = point_in_rect(mx, my, px, y, pw, 24);
            uint32_t row_bg = active ? T_ACCENT : (hovered ? T_PANEL_HI : T_PANEL);
            uint32_t row_fg = active ? T_BG : T_TEXT;
            fb_fill_rect(px, y, pw, 24, row_bg);
            fb_text(px + 10, y + 6, theme_name(i), row_fg);
            if (active) fb_text(px + pw - 56, y + 6, "Active", row_fg);
            y += 26;
        }
        break;

    case 1: /* Display */
        fb_text(px, y, "Display", T_ACCENT); y += 28;
        draw_setting_row(px + 8, y, pw, "Resolution", "1280 x 960"); y += 20;
        draw_setting_row(px + 8, y, pw, "Color Depth", "32-bit True Color"); y += 20;
        draw_setting_row(px + 8, y, pw, "Refresh Rate", "60 Hz"); y += 20;
        draw_setting_row(px + 8, y, pw, "Framebuffer", "Linear VESA VBE"); y += 20;
        draw_setting_row(px + 8, y, pw, "Font", "8x16 CP437 bitmap"); y += 28;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 10;
        fb_text(px + 8, y, "Brightness", T_MUTED); y += 20;
        draw_slider(px + 8, y, pw - 60, 75);
        break;

    case 2: /* Network */
        fb_text(px, y, "Network", T_ACCENT); y += 28;
        fb_fill_rect(px + 8, y, 8, 8, T_OK);
        fb_text(px + 22, y, "Connected — Ethernet", T_OK); y += 24;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 8;
        draw_setting_row(px + 8, y, pw, "IPv4 Address", "10.0.2.15"); y += 20;
        draw_setting_row(px + 8, y, pw, "Subnet Mask", "255.255.255.0"); y += 20;
        draw_setting_row(px + 8, y, pw, "Gateway", "10.0.2.2"); y += 20;
        draw_setting_row(px + 8, y, pw, "DNS Server", "10.0.2.3"); y += 20;
        draw_setting_row(px + 8, y, pw, "MAC Address", "52:54:00:12:34:56"); y += 20;
        draw_setting_row(px + 8, y, pw, "NIC Driver", "NE2000 / RTL8139"); y += 20;
        draw_setting_row(px + 8, y, pw, "Stack", "IPv4 / ICMP / UDP / DHCP / DNS");
        break;

    case 3: /* Sound */
        fb_text(px, y, "Sound", T_ACCENT); y += 28;
        fb_text(px + 8, y, "No audio hardware detected.", T_MUTED); y += 20;
        fb_text(px + 8, y, "PC speaker beep only.", T_DIM);
        break;

    case 4: /* Power */
        fb_text(px, y, "Power", T_ACCENT); y += 28;
        draw_setting_row(px + 8, y, pw, "ACPI", "Detected"); y += 20;
        draw_setting_row(px + 8, y, pw, "Shutdown", "ACPI S5 supported"); y += 20;
        draw_setting_row(px + 8, y, pw, "Reboot", "ACPI reset register"); y += 20;
        draw_setting_row(px + 8, y, pw, "Power Source", "AC / Virtual"); y += 28;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 8;
        /* shutdown/reboot buttons */
        fb_fill_rect(px + 8, y, 90, 24, T_ERR);
        fb_text(px + 18, y + 6, "Shut Down", T_TEXT);
        fb_fill_rect(px + 108, y, 80, 24, T_PANEL_HI);
        fb_draw_rect(px + 108, y, 80, 24, T_DIM);
        fb_text(px + 120, y + 6, "Reboot", T_TEXT);
        break;

    case 5: /* Storage */
        fb_text(px, y, "Storage", T_ACCENT); y += 28;
        draw_setting_row(px + 8, y, pw, "ATA", "Legacy PIO mode"); y += 20;
        draw_setting_row(px + 8, y, pw, "AHCI", "SATA DMA 48-bit LBA"); y += 20;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 8;
        fb_text(px + 8, y, "Filesystems", T_MUTED); y += 18;
        draw_setting_row(px + 8, y, pw, "/", "ramfs (4 MB)"); y += 18;
        draw_setting_row(px + 8, y, pw, "/dev", "devfs"); y += 18;
        /* usage bar */
        fb_text(px + 8, y, "Disk Usage", T_MUTED); y += 18;
        fb_fill_rect(px + 8, y, pw - 16, 10, T_DIM);
        fb_fill_rect(px + 8, y, (pw - 16) / 4, 10, T_ACCENT);
        fb_text(px + pw - 40, y - 2, "24%", T_TEXT);
        break;

    case 6: /* Keyboard */
        fb_text(px, y, "Keyboard", T_ACCENT); y += 28;
        draw_setting_row(px + 8, y, pw, "Layout", "US QWERTY"); y += 20;
        draw_setting_row(px + 8, y, pw, "Input", "PS/2 + USB HID"); y += 20;
        draw_setting_row(px + 8, y, pw, "Repeat Rate", "30 chars/sec"); y += 20;
        draw_setting_row(px + 8, y, pw, "Repeat Delay", "500 ms"); y += 28;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 8;
        fb_text(px + 8, y, "Shortcuts", T_MUTED); y += 18;
        draw_setting_row(px + 8, y, pw, "F1", "Open Terminal"); y += 18;
        draw_setting_row(px + 8, y, pw, "Arrow Keys", "Move cursor"); y += 18;
        draw_setting_row(px + 8, y, pw, "Enter", "Click"); y += 18;
        draw_setting_row(px + 8, y, pw, "Escape", "Release click");
        break;

    case 7: /* Mouse */
        fb_text(px, y, "Mouse", T_ACCENT); y += 28;
        draw_setting_row(px + 8, y, pw, "Type", "PS/2 + VBoxGuest"); y += 20;
        draw_setting_row(px + 8, y, pw, "Buttons", "3 (L, R, Middle)"); y += 20;
        draw_setting_row(px + 8, y, pw, "Sample Rate", "100/sec"); y += 28;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 10;
        fb_text(px + 8, y, "Pointer Speed", T_MUTED); y += 20;
        draw_slider(px + 8, y, pw - 60, 50); y += 24;
        fb_text(px + 8, y, "Sensitivity", T_MUTED); y += 20;
        draw_slider(px + 8, y, pw - 60, 65); y += 28;
        fb_fill_rect(px, y, pw, 40, T_PANEL);
        fb_text(px + 10, y + 6, "Left-handed Mode", T_TEXT);
        fb_text(px + 10, y + 22, "Swap left and right buttons", T_DIM);
        draw_toggle(px + pw - 46, y + 12, 0); y += 48;
        fb_fill_rect(px, y, pw, 40, T_PANEL);
        fb_text(px + 10, y + 6, "Natural Scrolling", T_TEXT);
        fb_text(px + 10, y + 22, "Content follows finger direction", T_DIM);
        draw_toggle(px + pw - 46, y + 12, 0);
        break;

    case 8: /* Privacy */
        fb_text(px, y, "Privacy & Security", T_ACCENT); y += 28;
        fb_fill_rect(px, y, pw, 40, T_PANEL);
        fb_text(px + 10, y + 6, "Telemetry", T_TEXT);
        fb_text(px + 10, y + 22, "No data collected", T_DIM);
        draw_toggle(px + pw - 46, y + 12, 0); y += 48;
        fb_fill_rect(px, y, pw, 40, T_PANEL);
        fb_text(px + 10, y + 6, "Crash Reports", T_TEXT);
        fb_text(px + 10, y + 22, "Disabled", T_DIM);
        draw_toggle(px + pw - 46, y + 12, 0); y += 48;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 8;
        draw_setting_row(px + 8, y, pw, "Usage Data", "None collected"); y += 20;
        draw_setting_row(px + 8, y, pw, "Network Access", "Local only"); y += 20;
        draw_setting_row(px + 8, y, pw, "Camera", "Not detected"); y += 20;
        draw_setting_row(px + 8, y, pw, "Microphone", "Not detected"); y += 20;
        draw_setting_row(px + 8, y, pw, "Location", "Disabled");
        break;

    case 9: /* Updates */
        fb_text(px, y, "Software Updates", T_ACCENT); y += 28;
        fb_fill_rect(px + 8, y, pw - 16, 60, T_PANEL);
        fb_text(px + 18, y + 8, "Darknode OS v0.1.0", T_TEXT);
        fb_text(px + 18, y + 24, "Your system is up to date.", T_OK);
        fb_text(px + 18, y + 40, "Last checked: Never", T_DIM);
        y += 68;
        fb_fill_rect(px + 8, y, 140, 24, T_ACCENT);
        fb_text(px + 18, y + 6, "Check for Updates", T_BG); y += 36;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 8;
        draw_setting_row(px + 8, y, pw, "Channel", "Stable"); y += 20;
        fb_fill_rect(px, y, pw, 40, T_PANEL);
        fb_text(px + 10, y + 6, "Auto-Update", T_TEXT);
        fb_text(px + 10, y + 22, "Download and install automatically", T_DIM);
        draw_toggle(px + pw - 46, y + 12, 0); y += 48;
        draw_setting_row(px + 8, y, pw, "APT Source", "apt.darknode.ai");
        break;

    case 10: /* System */
        fb_text(px, y, "System", T_ACCENT); y += 28;
        draw_setting_row(px + 8, y, pw, "Kernel", "Darknode OS v0.1.0"); y += 20;
        draw_setting_row(px + 8, y, pw, "Architecture", "x86 (i386)"); y += 20;
        draw_setting_row(px + 8, y, pw, "Scheduler", "Round-robin preemptive"); y += 20;
        draw_setting_row(px + 8, y, pw, "Syscalls", "INT 0x80 (11 handlers)"); y += 20;
        draw_setting_row(px + 8, y, pw, "Heap", "4 MiB at 0x400000"); y += 20;
        draw_setting_row(px + 8, y, pw, "Timer", "PIT 1000 Hz"); y += 20;
        draw_setting_row(px + 8, y, pw, "Interrupts", "PIC 8259 (remapped)"); y += 20;
        draw_setting_row(px + 8, y, pw, "Hostname", "darknode-os"); y += 20;
        draw_setting_row(px + 8, y, pw, "User", "root (Administrator)"); y += 20;
        draw_setting_row(px + 8, y, pw, "Shell", "/bin/darksh");
        break;

    case 11: /* About */
        fb_text(px, y, "About", T_ACCENT); y += 28;
        fb_text(px + 8, y, "D A R K N O D E   O S", T_ACCENT); y += 24;
        fb_fill_rect(px + 8, y, 180, 2, T_ACCENT); y += 10;
        fb_text(px + 8, y, "Version 0.1.0", T_TEXT); y += 20;
        fb_text(px + 8, y, "Custom x86 kernel", T_MUTED); y += 16;
        fb_text(px + 8, y, "Built from scratch — no Linux,", T_MUTED); y += 16;
        fb_text(px + 8, y, "no borrowed code.", T_MUTED); y += 24;
        fb_fill_rect(px, y, pw, 1, T_DIM); y += 10;
        draw_setting_row(px + 8, y, pw, "Drivers", "11"); y += 18;
        draw_setting_row(px + 8, y, pw, "GUI Apps", "12"); y += 18;
        draw_setting_row(px + 8, y, pw, "Shell Cmds", "28"); y += 18;
        draw_setting_row(px + 8, y, pw, "Themes", "6"); y += 24;
        fb_text(px + 8, y, "github.com/cashzombs-stack", T_ACCENT); y += 16;
        fb_text(px + 8, y, "By cashzombs-stack", T_DIM);
        break;
    }
}

static void draw_texteditor(int wid, int cx, int cy, int cw, int ch) {
    (void)wid;
    /* toolbar */
    fb_fill_rect(cx, cy, cw, 22, T_PANEL);
    fb_text(cx + 4, cy + 5, "File  Edit  View", T_DIM);
    fb_fill_rect(cx, cy + 22, cw, 1, T_DIM);

    /* text area */
    int ty = cy + 28;
    fb_text(cx + 4, ty, "untitled.txt", T_MUTED); ty += 18;
    fb_fill_rect(cx, ty, cw, 1, T_DIM); ty += 6;

    /* line numbers + content area */
    fb_fill_rect(cx, ty, 32, ch - 34, T_PANEL);
    for (int i = 0; i < (ch - 40) / 16 && i < 30; i++) {
        char ln[4];
        ln[0] = '0' + ((i + 1) / 10);
        ln[1] = '0' + ((i + 1) % 10);
        ln[2] = '\0';
        fb_text(cx + 8, ty + i * 16, ln, T_DIM);
    }

    /* cursor blink */
    int blink = (timer_get_ticks() / 500) % 2;
    fb_text(cx + 38, ty, "Type here...", T_DIM);
    if (blink) fb_glyph(cx + 38 + 12 * 8, ty, '_', T_ACCENT);
}

/* ── new app callbacks ── */

static void draw_browser(int wid, int cx, int cy, int cw, int ch) {
    (void)wid;
    int y = cy;

    /* Chrome-style tab bar */
    fb_fill_rect(cx, y, cw, 28, T_PANEL);
    /* active tab */
    fb_fill_rect(cx + 4, y + 4, 180, 24, T_BODY);
    fb_draw_rect(cx + 4, y + 4, 180, 24, T_DIM);
    fb_fill_rect(cx + 4, y + 27, 180, 1, T_BODY); /* merge tab into body */
    fb_text(cx + 12, y + 10, "Darknode AI", T_TEXT);
    fb_glyph(cx + 168, y + 10, 'x', T_DIM);
    /* new tab button */
    fb_text(cx + 192, y + 10, "+", T_DIM);
    y += 28;

    /* address bar row */
    fb_fill_rect(cx, y, cw, 30, T_BODY);
    /* nav buttons */
    fb_text(cx + 8, y + 9, "<", T_DIM);
    fb_text(cx + 24, y + 9, ">", T_DIM);
    fb_text(cx + 40, y + 9, "R", T_DIM);
    /* URL bar */
    fb_fill_rect(cx + 58, y + 5, cw - 120, 20, T_PANEL);
    fb_draw_rect(cx + 58, y + 5, cw - 120, 20, T_DIM);
    fb_text(cx + 64, y + 9, "darknode.ai", T_ACCENT);
    /* menu dots */
    fb_text(cx + cw - 24, y + 9, ":", T_DIM);
    y += 30;

    /* separator */
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 1;

    /* page content area — white-ish background */
    int page_h = ch - (y - cy) - 22;
    fb_fill_rect(cx, y, cw, page_h, T_BG);

    int py = y + 16;
    int px = cx + 24;

    /* hero section */
    fb_fill_rect(px, py, cw - 48, 2, T_ACCENT);
    py += 10;
    fb_text(px, py, "D A R K N O D E", T_ACCENT); py += 24;
    fb_text(px, py, "Security Operating System", T_TEXT); py += 20;
    fb_text(px, py, "The platform for ethical hacking", T_DIM); py += 16;
    fb_text(px, py, "and cybersecurity education.", T_DIM); py += 28;

    /* CTA buttons */
    fb_fill_rect(px, py, 110, 24, T_ACCENT);
    fb_text(px + 12, py + 6, "Get Started", T_BG);
    fb_fill_rect(px + 120, py, 100, 24, T_PANEL_HI);
    fb_draw_rect(px + 120, py, 100, 24, T_DIM);
    fb_text(px + 132, py + 6, "Download", T_TEXT);
    py += 36;

    /* stats row */
    fb_fill_rect(px, py, cw - 48, 1, T_DIM); py += 10;
    fb_text(px, py, "449K+", T_ACCENT);
    fb_text(px + 56, py, "lines", T_DIM);
    fb_text(px + 110, py, "120+", T_ACCENT);
    fb_text(px + 150, py, "tools", T_DIM);
    fb_text(px + 200, py, "6", T_ACCENT);
    fb_text(px + 216, py, "engines", T_DIM);
    fb_text(px + 280, py, "34", T_ACCENT);
    fb_text(px + 304, py, "modules", T_DIM);
    py += 24;

    /* features */
    fb_text(px, py, "> Nexus AI coding agent", T_TEXT); py += 16;
    fb_text(px, py, "> Multi-engine: Claude + Ollama", T_TEXT); py += 16;
    fb_text(px, py, "> 80+ security toolkit scripts", T_TEXT); py += 16;
    fb_text(px, py, "> Custom x86 kernel + GUI", T_TEXT); py += 16;
    fb_text(px, py, "> 100% local and private", T_TEXT);

    /* status bar at bottom */
    fb_fill_rect(cx, y + page_h, cw, 22, T_PANEL);
    fb_fill_rect(cx, y + page_h, 8, 8, T_OK);
    fb_text(cx + 4, y + page_h + 5, "Secure", T_OK);
    fb_text(cx + 64, y + page_h + 5, "darknode.ai", T_DIM);
    fb_text(cx + cw - 70, y + page_h + 5, "DNS: OK", T_OK);
}

static void draw_taskmanager(int wid, int cx, int cy, int cw, int ch) {
    (void)wid;
    int y = cy;
    /* tabs */
    fb_fill_rect(cx, y, cw, 24, T_PANEL);
    fb_fill_rect(cx, y, 80, 24, T_ACCENT);
    fb_text(cx + 8, y + 6, "Processes", T_BG);
    fb_text(cx + 92, y + 6, "Memory", T_DIM);
    fb_text(cx + 156, y + 6, "Network", T_DIM);
    y += 28;
    /* column headers */
    fb_text(cx + 4, y, "PID", T_DIM);
    fb_text(cx + 40, y, "Name", T_DIM);
    fb_text(cx + cw - 100, y, "State", T_DIM);
    fb_text(cx + cw - 44, y, "CPU", T_DIM);
    y += 16;
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 4;
    /* process list */
    static const char *procs[][4] = {
        {"0", "idle",       "Running",  "0.1%"},
        {"1", "kernel",     "Running",  "2.4%"},
        {"2", "shell",      "Sleeping", "0.0%"},
        {"3", "scheduler",  "Running",  "0.8%"},
        {"4", "timer",      "Running",  "0.3%"},
        {"5", "keyboard",   "Waiting",  "0.0%"},
        {"6", "mouse",      "Waiting",  "0.0%"},
        {"7", "ne2000",     "Running",  "0.5%"},
        {"8", "ata",        "Sleeping", "0.0%"},
        {"9", "gui",        "Running",  "4.2%"},
        {"10","framebuffer","Running",  "1.1%"},
        {"11","dhcp",       "Sleeping", "0.0%"},
    };
    for (int i = 0; i < 12 && y + 18 < cy + ch - 30; i++) {
        uint32_t bg = (i % 2) ? T_BODY : T_PANEL;
        fb_fill_rect(cx, y, cw, 18, bg);
        fb_text(cx + 4, y + 3, procs[i][0], T_MUTED);
        fb_text(cx + 40, y + 3, procs[i][1], T_TEXT);
        uint32_t sc = (procs[i][2][0] == 'R') ? T_OK : T_DIM;
        fb_text(cx + cw - 100, y + 3, procs[i][2], sc);
        fb_text(cx + cw - 44, y + 3, procs[i][3], T_ACCENT);
        y += 18;
    }
    /* footer stats */
    y = cy + ch - 24;
    fb_fill_rect(cx, y, cw, 24, T_PANEL);
    fb_text(cx + 4, y + 6, "12 processes", T_DIM);
    fb_text(cx + 120, y + 6, "CPU: 9.4%", T_ACCENT);
    fb_text(cx + 220, y + 6, "Mem: 38/128 MB", T_TEXT);
}

static void draw_diskmanager(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;
    fb_text(cx, y, "Disk Manager", T_ACCENT); y += 24;
    /* drive list */
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 6;
    fb_text(cx, y, "Drives", T_ACCENT); y += 20;
    /* drive 1 */
    fb_fill_rect(cx, y, cw, 60, T_PANEL);
    fb_draw_rect(cx, y, cw, 60, T_DIM);
    fb_text(cx + 8, y + 4, "/dev/hda - QEMU HARDDISK", T_TEXT);
    fb_text(cx + 8, y + 20, "Type: ATA PIO", T_DIM);
    fb_text(cx + 8, y + 36, "Size: 2 GB", T_DIM);
    /* capacity bar */
    fb_fill_rect(cx + 160, y + 36, cw - 180, 12, FB_DARKGRAY);
    fb_fill_rect(cx + 160, y + 36, (cw - 180) / 4, 12, T_ACCENT);
    fb_text(cx + cw - 40, y + 36, "24%", T_TEXT);
    y += 68;
    /* drive 2 */
    fb_fill_rect(cx, y, cw, 60, T_PANEL);
    fb_draw_rect(cx, y, cw, 60, T_DIM);
    fb_text(cx + 8, y + 4, "/dev/hdb - CDROM", T_TEXT);
    fb_text(cx + 8, y + 20, "Type: ATAPI", T_DIM);
    fb_text(cx + 8, y + 36, "Media: darknode-os.iso (12 MB)", T_DIM);
    y += 68;
    /* filesystem info */
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 6;
    fb_text(cx, y, "Filesystems", T_ACCENT); y += 20;
    fb_text(cx + 8, y, "/        ramfs    4 MB    mounted", T_TEXT); y += 16;
    fb_text(cx + 8, y, "/dev     devfs    -       mounted", T_TEXT); y += 16;
    fb_text(cx + 8, y, "/tmp     ramfs    1 MB    mounted", T_DIM);
}

static void draw_firewall(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;
    fb_text(cx, y, "Firewall", T_ACCENT); y += 20;
    /* status */
    fb_fill_rect(cx, y, 8, 8, T_OK);
    fb_text(cx + 14, y - 2, "Active", T_OK); y += 20;
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 6;
    /* rules */
    fb_text(cx, y, "Rules", T_ACCENT); y += 18;
    fb_text(cx + 4, y, "#", T_DIM);
    fb_text(cx + 24, y, "Action", T_DIM);
    fb_text(cx + 88, y, "Proto", T_DIM);
    fb_text(cx + 140, y, "Port", T_DIM);
    fb_text(cx + 200, y, "Source", T_DIM);
    y += 14; fb_fill_rect(cx, y, cw, 1, T_DIM); y += 4;

    static const char *rules[][5] = {
        {"1", "ALLOW", "TCP", "22",   "10.0.2.0/24"},
        {"2", "ALLOW", "TCP", "80",   "any"},
        {"3", "ALLOW", "TCP", "443",  "any"},
        {"4", "ALLOW", "UDP", "53",   "10.0.2.3"},
        {"5", "ALLOW", "ICMP","*",    "any"},
        {"6", "DROP",  "TCP", "23",   "any"},
        {"7", "DROP",  "TCP", "445",  "any"},
        {"8", "DROP",  "*",   "*",    "0.0.0.0/0"},
    };
    for (int i = 0; i < 8 && y + 18 < cy + ch; i++) {
        uint32_t bg = (i % 2) ? T_BODY : T_PANEL;
        fb_fill_rect(cx, y, cw, 16, bg);
        fb_text(cx + 4, y + 2, rules[i][0], T_MUTED);
        uint32_t ac = (rules[i][1][0] == 'A') ? T_OK : T_ERR;
        fb_text(cx + 24, y + 2, rules[i][1], ac);
        fb_text(cx + 88, y + 2, rules[i][2], T_TEXT);
        fb_text(cx + 140, y + 2, rules[i][3], T_TEXT);
        fb_text(cx + 200, y + 2, rules[i][4], T_DIM);
        y += 16;
    }
}

static void draw_hashtools(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)ch;
    int y = cy;
    fb_text(cx, y, "Hash Tools", T_ACCENT); y += 24;
    /* input */
    fb_text(cx, y, "Input:", T_DIM); y += 16;
    fb_fill_rect(cx, y, cw, 22, T_PANEL);
    fb_draw_rect(cx, y, cw, 22, T_DIM);
    fb_text(cx + 6, y + 5, "darknode", T_TEXT);
    y += 30;
    /* results */
    fb_fill_rect(cx, y, cw, 1, T_DIM); y += 6;
    fb_text(cx, y, "MD5", T_DIM); y += 14;
    fb_text(cx + 8, y, "a3f2b8c1d4e5f6a7b8c9d0e1f2a3b4c5", T_MUTED); y += 18;
    fb_text(cx, y, "SHA-1", T_DIM); y += 14;
    fb_text(cx + 8, y, "da39a3ee5e6b4b0d3255bfef95601890afd80709", T_MUTED); y += 18;
    fb_text(cx, y, "SHA-256", T_DIM); y += 14;
    fb_text(cx + 8, y, "e3b0c44298fc1c149afbf4c8996fb924", T_MUTED); y += 14;
    fb_text(cx + 8, y, "27ae41e4649b934ca495991b7852b855", T_MUTED); y += 22;
    /* buttons */
    fb_fill_rect(cx, y, 72, 22, T_ACCENT);
    fb_text(cx + 12, y + 5, "Compute", T_BG);
    fb_fill_rect(cx + 80, y, 56, 22, T_PANEL_HI);
    fb_draw_rect(cx + 80, y, 56, 22, T_DIM);
    fb_text(cx + 92, y + 5, "Clear", T_TEXT);
}

static void __attribute__((unused)) draw_about(int wid, int cx, int cy, int cw, int ch) {
    (void)wid; (void)cw; (void)ch;
    int y = cy + 16;
    fb_text(cx + 8, y, "D A R K N O D E   O S", T_ACCENT); y += 28;
    fb_fill_rect(cx + 8, y, 180, 2, T_ACCENT); y += 12;
    fb_text(cx + 8, y, "Version 0.1.0", T_TEXT); y += 20;
    fb_text(cx + 8, y, "Custom x86 kernel", T_DIM); y += 16;
    fb_text(cx + 8, y, "Built from scratch", T_DIM); y += 24;
    fb_fill_rect(cx + 8, y, cw - 16, 1, T_DIM); y += 10;
    fb_text(cx + 8, y, "Kernel:   x86 (i386)", T_TEXT); y += 16;
    fb_text(cx + 8, y, "Display:  1024x768x32", T_TEXT); y += 16;
    fb_text(cx + 8, y, "Memory:   128 MB", T_TEXT); y += 16;
    fb_text(cx + 8, y, "Net:      IPv4/ICMP/UDP", T_TEXT); y += 16;
    fb_text(cx + 8, y, "Disk:     ATA PIO", T_TEXT); y += 16;
    fb_text(cx + 8, y, "FS:       ramfs, devfs", T_TEXT); y += 24;
    fb_fill_rect(cx + 8, y, cw - 16, 1, T_DIM); y += 10;
    fb_text(cx + 8, y, "By cashzombs-stack", T_DIM); y += 16;
    fb_text(cx + 8, y, "github.com/cashzombs-stack", T_ACCENT);
}

/* ── public API ── */

static int dirty = 1;

void gui_mark_dirty(void) { dirty = 1; }

void gui_redraw(void) {
    int mx = mouse_get_x(), my = mouse_get_y();

    if (dirty) {
        /* Full redraw — something changed (window move, menu, click) */
        cursor_restore();
        draw_desktop();
        for (int i = 0; i < order_count; i++)
            draw_window(win_order[i]);
        draw_taskbar();
        draw_start_menu();
        /* Right-click context menu */
        if (rclick_open) {
            static const char *desk_items[] = { "Terminal", "File Manager", "Settings", "Refresh" };
            static const char *win_items[] = { "Minimize", "Maximize", "Close" };
            const char **items = (rclick_type == 0) ? desk_items : win_items;
            int n = (rclick_type == 0) ? 4 : 3;
            int mh = n * RCLICK_ITEM_H + 4;
            fb_fill_rect(rclick_x, rclick_y, RCLICK_W, mh, T_PANEL);
            fb_draw_rect(rclick_x, rclick_y, RCLICK_W, mh, T_ACCENT);
            for (int i = 0; i < n; i++) {
                int iy = rclick_y + 2 + i * RCLICK_ITEM_H;
                int hov = point_in_rect(mx, my, rclick_x, iy, RCLICK_W, RCLICK_ITEM_H);
                if (hov) fb_fill_rect(rclick_x + 2, iy, RCLICK_W - 4, RCLICK_ITEM_H, T_ACCENT);
                fb_text(rclick_x + 10, iy + 6, items[i], hov ? T_BG : T_TEXT);
            }
        }
        cursor_draw(mx, my);
        dirty = 0;
    } else if (mx != cursor_saved_x || my != cursor_saved_y) {
        /* Mouse moved only — just move the cursor */
        cursor_restore();
        cursor_draw(mx, my);
    }
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
    raise_window(id); dirty = 1;
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
    case 0: { int tid = gui_create_window("Terminal", ox, oy, 540, 340, 0);
        gui_window_print(tid,
            "Darknode OS v0.1.0 (tty1)\n"
            "Kernel: x86 custom | 128 MB RAM | 1024x768\n"
            "\n"
            "[ok] VFS mounted (ramfs at /)\n"
            "[ok] devfs at /dev\n"
            "[ok] ATA + AHCI disk drivers\n"
            "[ok] NE2000 + RTL8139 NIC\n"
            "[ok] TCP/IP stack ready\n"
            "[ok] ACPI power management\n"
            "[ok] USB UHCI + HID\n"
            "[ok] VBoxGuest mouse\n"
            "[ok] GUI framebuffer 1024x768x32\n"
            "\n"
            "root@darknode:~# _\n"
        );
        dirty = 1; break; }
    case 1: gui_create_window("File Manager", ox, oy, 440, 400, draw_filemanager); break;
    case 2: gui_create_window("Text Editor", ox, oy, 520, 400, draw_texteditor); break;
    case 3: gui_create_window("Darknode Browser", ox, oy, 600, 500, draw_browser); break;
    case 4: gui_create_window("Task Manager", ox, oy, 460, 420, draw_taskmanager); break;
    case 5: gui_create_window("Disk Manager", ox, oy, 440, 420, draw_diskmanager); break;
    case 6: gui_create_window("Calculator", ox, oy, 240, 280, draw_calculator); break;
    case 7: gui_create_window("Network", ox, oy, 420, 460, draw_network); break;
    case 8: gui_create_window("Firewall", ox, oy, 420, 380, draw_firewall); break;
    case 9: gui_create_window("Hash Tools", ox, oy, 400, 360, draw_hashtools); break;
    case 10: gui_create_window("Settings", ox, oy, 560, 480, draw_settings); break;
    case 11: gui_create_window("About Darknode", ox, oy, 400, 400, draw_welcome); break;
    }
}

/* ── event handling ── */

#define RESIZE_GRAB 6
#define MIN_WIN_W 200
#define MIN_WIN_H 120
#define EDGE_L 1
#define EDGE_R 2
#define EDGE_T 4
#define EDGE_B 8

static int hit_test_resize(gui_window_t *w, int mx, int my) {
    int edge = 0;
    if (mx >= w->x && mx < w->x + RESIZE_GRAB) edge |= EDGE_L;
    if (mx >= w->x + w->w - RESIZE_GRAB && mx < w->x + w->w) edge |= EDGE_R;
    if (my >= w->y && my < w->y + RESIZE_GRAB) edge |= EDGE_T;
    if (my >= w->y + w->h - RESIZE_GRAB && my < w->y + w->h) edge |= EDGE_B;
    return edge;
}

static int hit_test_window(int mx, int my, int *part) {
    for (int i = order_count - 1; i >= 0; i--) {
        int id = win_order[i];
        gui_window_t *w = &windows[id];
        if (!w->visible) continue;
        /* Check resize zone (slightly outside window bounds too) */
        if (point_in_rect(mx, my, w->x - 2, w->y - 2, w->w + 4, w->h + 4)) {
            int edge = hit_test_resize(w, mx, my);
            if (edge) { *part = 3; windows[id].resize_edge = edge; return id; }
            int bsz = minimalist_mode ? 16 : 18;
            int bgap = bsz + 4;
            int by2 = w->y + border_w() + (title_h() - bsz) / 2;
            /* Close button */
            if (point_in_rect(mx, my, w->x + w->w - bgap - 2, by2, bsz, bsz)) { *part = 2; return id; }
            /* Maximize button */
            if (point_in_rect(mx, my, w->x + w->w - bgap * 2 - 2, by2, bsz, bsz)) { *part = 5; return id; }
            /* Minimize button */
            if (point_in_rect(mx, my, w->x + w->w - bgap * 3 - 2, by2, bsz, bsz)) { *part = 4; return id; }
            if (my < w->y + (minimalist_mode ? title_h() : 28)) { *part = 1; return id; }
            if (point_in_rect(mx, my, w->x, w->y, w->w, w->h)) { *part = 0; return id; }
        }
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
            int menu_y = sh - tb_h() - menu_h;
            if (point_in_rect(mx, my, 4, menu_y, MENU_W, menu_h)) {
                int idx = (my - menu_y - 4) / MENU_ITEM_H;
                if (idx >= 0 && idx < MENU_COUNT) {
                    launch_app(idx); dirty = 1;
                    menu_open = 0; dirty = 1;
                }
            } else {
                menu_open = 0; dirty = 1;
            }
            goto done;
        }

        if (my >= sh - tb_h()) {
            if (mx < 48) {
                menu_open = !menu_open; dirty = 1;
            } else {
                int id = hit_test_taskbar_window(mx);
                if (id >= 0) { windows[id].visible = 1; raise_window(id); dirty = 1; }
            }
        } else {
            /* check desktop icon clicks */
            int icon_x = 16;
            int icon_y = 40;
            int icon_hit = 0;
            for (int i = 0; i < ICON_COUNT; i++) {
                if (point_in_rect(mx, my, icon_x + 16, icon_y, 40, 32)) {
                    launch_app(icon_app_map[i]);
                    icon_hit = 1; dirty = 1;
                    break;
                }
                icon_y += ICON_H + 8;
            }
            if (icon_hit) goto done;

            int part;
            int id = hit_test_window(mx, my, &part);
            if (id >= 0) {
                raise_window(id); dirty = 1;
                if (part == 2) { gui_close_window(id); dirty = 1; }
                else if (part == 4) { windows[id].visible = 0; dirty = 1; }
                else if (part == 5) {
                    /* Maximize / restore */
                    gui_window_t *mw = &windows[id];
                    if (mw->maximized) {
                        mw->x = mw->saved_x; mw->y = mw->saved_y;
                        mw->w = mw->saved_w; mw->h = mw->saved_h;
                        mw->maximized = 0;
                    } else {
                        mw->saved_x = mw->x; mw->saved_y = mw->y;
                        mw->saved_w = mw->w; mw->saved_h = mw->h;
                        mw->x = 0; mw->y = 22;
                        mw->w = fb_width();
                        mw->h = fb_height() - 22 - tb_h();
                        mw->maximized = 1;
                    }
                    dirty = 1;
                }
                else if (part == 0 && windows[id].draw_content == draw_calculator) {
                    /* Calculator button clicks */
                    gui_window_t *cw2 = &windows[id];
                    int brd2 = border_w();
                    int body_x2 = cw2->x + brd2 + 4;
                    int body_y2 = cw2->y + title_h() + brd2 + 4;
                    int body_w2 = cw2->w - brd2 * 2 - 8;
                    int btn_w2 = (body_w2 - 12) / 4;
                    int grid_y = body_y2 + 44;
                    static const char btns[] = "789/456*123-0.=+";
                    if (my >= grid_y && mx >= body_x2) {
                        int row = (my - grid_y) / 36;
                        int col2 = (mx - body_x2) / (btn_w2 + 4);
                        if (row >= 0 && row < 4 && col2 >= 0 && col2 < 4) {
                            char b = btns[row * 4 + col2];
                            if (b >= '0' && b <= '9') calc_digit(b - '0');
                            else if (b == '=') calc_equals();
                            else if (b == '+' || b == '-' || b == '*' || b == '/') calc_operator(b);
                            dirty = 1;
                        }
                    }
                }
                else if (part == 0 && windows[id].draw_content == draw_settings) {
                    int body_x = windows[id].x + 1;
                    int body_y = windows[id].y + title_h() + 1;
                    int sidebar_w = 110;

                    if (mx < body_x + sidebar_w + 4) {
                        /* Sidebar tab click */
                        int tab_idx = (my - body_y - 8) / 26;
                        if (tab_idx >= 0 && tab_idx < 12) {
                            settings_tab = tab_idx;
                            dirty = 1;
                        }
                    } else if (settings_tab == 0) {
                        /* Appearance tab clicks */
                        int px = body_x + sidebar_w + 16 + 4; (void)px;
                        int pw = windows[id].w - 2 - sidebar_w - 32; (void)pw;
                        int toggle_y = body_y + 8 + 28; /* minimalist toggle row */

                        /* Minimalist mode toggle */
                        if (my >= toggle_y && my < toggle_y + 40) {
                            minimalist_mode = !minimalist_mode;
                            dirty = 1;
                        }
                        /* Theme rows — start after toggle (48px) + label (18px) */
                        int theme_start = toggle_y + 48 + 18;
                        if (my >= theme_start) {
                            int idx = (my - theme_start) / 26;
                            if (idx >= 0 && idx < theme_count()) {
                                theme_set(idx);
                                dirty = 1;
                            }
                        }
                    }
                }
                else if (part == 3) {
                    windows[id].resizing = 1;
                    mouse_down_win = id;
                }
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
        if (w->resizing) {
            int e = w->resize_edge;
            int dx = mx - prev_mx, dy = my - prev_my;
            if (e & EDGE_R) { w->w += dx; if (w->w < MIN_WIN_W) w->w = MIN_WIN_W; }
            if (e & EDGE_B) { w->h += dy; if (w->h < MIN_WIN_H) w->h = MIN_WIN_H; }
            if (e & EDGE_L) { int nw = w->w - dx; if (nw >= MIN_WIN_W) { w->x += dx; w->w = nw; } }
            if (e & EDGE_T) { int nh = w->h - dy; if (nh >= MIN_WIN_H) { w->y += dy; w->h = nh; } }
            dirty = 1;
        } else if (w->dragging) {
            w->x = mx - w->drag_ox; dirty = 1;
            w->y = my - w->drag_oy;
            if (w->x < 0) w->x = 0;
            if (w->y < 0) w->y = 0;
            int sw = fb_width();
            if (w->x + w->w > sw) w->x = sw - w->w;
            if (w->y + w->h > sh - (minimalist_mode ? 32 : 38)) w->y = sh - (minimalist_mode ? 32 : 38) - w->h;
        }
    }

    if (released) {
        if (mouse_down_win >= 0) {
            gui_window_t *rw = &windows[mouse_down_win];
            if (rw->dragging && !rw->maximized) {
                int sw = fb_width();
                /* Window snap to edges */
                if (rw->x <= 0) {
                    /* Snap left half */
                    rw->saved_x = rw->x; rw->saved_y = rw->y;
                    rw->saved_w = rw->w; rw->saved_h = rw->h;
                    rw->x = 0; rw->y = 22;
                    rw->w = sw / 2; rw->h = fb_height() - 22 - tb_h();
                    rw->maximized = 2; /* 2 = snapped, not fully maximized */
                    dirty = 1;
                } else if (rw->x + rw->w >= sw) {
                    /* Snap right half */
                    rw->saved_x = rw->x; rw->saved_y = rw->y;
                    rw->saved_w = rw->w; rw->saved_h = rw->h;
                    rw->x = sw / 2; rw->y = 22;
                    rw->w = sw / 2; rw->h = fb_height() - 22 - tb_h();
                    rw->maximized = 2;
                    dirty = 1;
                } else if (rw->y <= 22) {
                    /* Snap maximize */
                    rw->saved_x = rw->x; rw->saved_y = rw->y;
                    rw->saved_w = rw->w; rw->saved_h = rw->h;
                    rw->x = 0; rw->y = 22;
                    rw->w = sw; rw->h = fb_height() - 22 - tb_h();
                    rw->maximized = 1;
                    dirty = 1;
                }
            }
            rw->dragging = 0;
            rw->resizing = 0;
            mouse_down_win = -1;
        }
    }

    /* Right-click handling */
    int rpressed = (mb & 2) && !(prev_mb & 2);
    if (rpressed) {
        if (rclick_open) {
            rclick_open = 0; dirty = 1;
        } else {
            int part;
            int id = hit_test_window(mx, my, &part);
            if (id >= 0 && part == 1) {
                /* Right-click on title bar */
                rclick_open = 1; rclick_x = mx; rclick_y = my;
                rclick_type = 1; rclick_win = id;
                dirty = 1;
            } else if (id < 0 && my < sh - tb_h()) {
                /* Right-click on desktop */
                rclick_open = 1; rclick_x = mx; rclick_y = my;
                rclick_type = 0; rclick_win = -1;
                dirty = 1;
            }
        }
    }

    /* Left-click on right-click menu items */
    if (pressed && rclick_open) {
        int n_items = (rclick_type == 0) ? 4 : 3;
        int menu_h2 = n_items * RCLICK_ITEM_H + 4;
        if (point_in_rect(mx, my, rclick_x, rclick_y, RCLICK_W, menu_h2)) {
            int idx = (my - rclick_y - 2) / RCLICK_ITEM_H;
            if (rclick_type == 0) {
                /* Desktop: Terminal, File Manager, Settings, Refresh */
                if (idx == 0) launch_app(0);
                else if (idx == 1) launch_app(1);
                else if (idx == 2) launch_app(10);
                /* idx==3 refresh = just redraw */
            } else if (rclick_win >= 0) {
                /* Window: Minimize, Maximize, Close */
                if (idx == 0) windows[rclick_win].visible = 0;
                else if (idx == 1) {
                    gui_window_t *mw = &windows[rclick_win];
                    if (mw->maximized) {
                        mw->x = mw->saved_x; mw->y = mw->saved_y;
                        mw->w = mw->saved_w; mw->h = mw->saved_h;
                        mw->maximized = 0;
                    } else {
                        mw->saved_x = mw->x; mw->saved_y = mw->y;
                        mw->saved_w = mw->w; mw->saved_h = mw->h;
                        mw->x = 0; mw->y = 22;
                        mw->w = fb_width();
                        mw->h = fb_height() - 22 - tb_h();
                        mw->maximized = 1;
                    }
                }
                else if (idx == 2) gui_close_window(rclick_win);
            }
            dirty = 1;
        }
        rclick_open = 0; dirty = 1;
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
    menu_open = 0; dirty = 1; cascade_off = 0;

    fb_clear(T_BG);

    /* Clean desktop — no windows open on boot. Click DN or desktop icons to launch apps. */

    gui_redraw();
}

/* Find the focused window that accepts text input (terminal = no draw_content callback) */
static int focused_terminal(void) {
    for (int i = order_count - 1; i >= 0; i--) {
        int id = win_order[i];
        if (windows[id].visible && windows[id].focused && !windows[id].draw_content)
            return id;
    }
    return -1;
}

void gui_run(void) {
    uint32_t last_redraw = 0;
    uint32_t last_clock = 0;
    while (1) {
        usb_hid_poll();
        mouse_poll();
        vbox_mouse_poll();
        handle_mouse();

        char key = keyboard_getchar();
        if (key) {
            int term = focused_terminal();
            if (term >= 0) {
                /* Typing into the focused terminal window */
                gui_window_t *w = &windows[term];
                if (key == '\b') {
                    /* Backspace — remove last char if not at prompt */
                    if (w->text_len > 0 && w->textbuf[w->text_len - 1] != '\n'
                        && w->textbuf[w->text_len - 1] != '#'
                        && w->textbuf[w->text_len - 1] != ' ') {
                        w->text_len--;
                        w->textbuf[w->text_len] = '\0';
                        dirty = 1;
                    }
                } else if (key == '\n') {
                    /* Enter — add newline + new prompt */
                    if (w->text_len < 4090) {
                        w->textbuf[w->text_len++] = '\n';
                        /* Echo a fake command response */
                        const char *prompt = "root@darknode:~# ";
                        int plen = 17;
                        for (int i = 0; i < plen && w->text_len < 4090; i++)
                            w->textbuf[w->text_len++] = prompt[i];
                        w->textbuf[w->text_len] = '\0';
                        dirty = 1;
                    }
                } else if (key >= 32 && key < 127) {
                    /* Printable character */
                    if (w->text_len < 4090) {
                        w->textbuf[w->text_len++] = key;
                        w->textbuf[w->text_len] = '\0';
                        dirty = 1;
                    }
                }
            } else {
                /* No terminal focused — use keyboard for cursor control */
                if (key == 0x3B) { /* F1 */
                    int id = gui_create_window("Terminal",
                        80 + (win_count * 20) % 200, 60 + (win_count * 20) % 150,
                        540, 340, 0);
                    gui_window_print(id,
                        "Darknode OS v0.1.0 (tty1)\n"
                        "root@darknode:~# ");
                    dirty = 1;
                }
            }
        }

        /* Auto-update clock every second */
        uint32_t now = timer_get_ticks();
        if (now - last_clock >= 1000) {
            dirty = 1;
            last_clock = now;
        }

        if (now - last_redraw >= 33) {
            gui_redraw();
            last_redraw = now;
        }
    }
}
