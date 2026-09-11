#ifndef GUI_H
#define GUI_H
#include "../include/types.h"

#define MAX_WINDOWS 16
#define TASKBAR_HEIGHT 32
#define TITLEBAR_HEIGHT 24

typedef struct {
    int x, y, w, h;
    char title[64];
    int visible;
    int focused;
    int dragging;
    int drag_ox, drag_oy;
    void (*draw_content)(int win_id, int cx, int cy, int cw, int ch);
    char textbuf[4096];
    int text_len;
    int scroll;
} gui_window_t;

void gui_init(void);
void gui_run(void);
int  gui_create_window(const char* title, int x, int y, int w, int h,
                        void (*draw_fn)(int, int, int, int, int));
void gui_close_window(int id);
void gui_window_print(int id, const char* text);
void gui_redraw(void);

#endif
