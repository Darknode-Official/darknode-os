#ifndef THEME_H
#define THEME_H
#include "../include/types.h"

typedef struct {
    const char *name;
    uint32_t bg;         /* desktop background */
    uint32_t panel;      /* taskbar, title bars */
    uint32_t panel_hi;   /* hovered/active panel */
    uint32_t accent;     /* accent color (buttons, highlights) */
    uint32_t text;       /* primary text */
    uint32_t muted;      /* secondary text */
    uint32_t dim;        /* borders, separators */
    uint32_t body;       /* window body */
    uint32_t ok;         /* success/connected */
    uint32_t err;        /* error/close button */
    uint32_t topbar;     /* top status bar */
    uint32_t cursor_fg;  /* cursor fill */
    uint32_t cursor_bg;  /* cursor outline */
} theme_t;

#define THEME_COUNT 6

void theme_init(void);
void theme_set(int index);
int  theme_get(void);
int  theme_count(void);
const theme_t *theme_current(void);
const char *theme_name(int index);

/* Shorthand accessors */
#define T_BG       (theme_current()->bg)
#define T_PANEL    (theme_current()->panel)
#define T_PANEL_HI (theme_current()->panel_hi)
#define T_ACCENT   (theme_current()->accent)
#define T_TEXT     (theme_current()->text)
#define T_MUTED    (theme_current()->muted)
#define T_DIM      (theme_current()->dim)
#define T_BODY     (theme_current()->body)
#define T_OK       (theme_current()->ok)
#define T_ERR      (theme_current()->err)
#define T_TOPBAR   (theme_current()->topbar)

#endif
