#include "theme.h"

/* All colors in BGRA (0x00BBGGRR) format for the framebuffer */

static const theme_t themes[THEME_COUNT] = {
    /* 0: Darknode (default — matches the distro) — dark gray + teal accent */
    {
        .name     = "Darknode",
        .bg       = 0x00201816, /* very dark warm gray */
        .panel    = 0x00302820, /* dark brown-gray */
        .panel_hi = 0x00403830, /* lighter */
        .accent   = 0x00D8C800, /* teal/cyan */
        .text     = 0x00F0E8E0, /* warm white */
        .muted    = 0x00908880, /* warm gray */
        .dim      = 0x00685848, /* border */
        .body     = 0x00281E1A, /* window body */
        .ok       = 0x0060DC40, /* green */
        .err      = 0x004040E8, /* red */
        .topbar   = 0x00282018, /* top bar */
        .cursor_fg= 0x00FFFFFF,
        .cursor_bg= 0x00000000,
    },
    /* 1: Hacker — black + green */
    {
        .name     = "Hacker",
        .bg       = 0x00080808,
        .panel    = 0x00181818,
        .panel_hi = 0x00282828,
        .accent   = 0x0000FF00, /* bright green */
        .text     = 0x0000DD00, /* green text */
        .muted    = 0x00008800,
        .dim      = 0x00003300,
        .body     = 0x00101010,
        .ok       = 0x0000FF00,
        .err      = 0x000000FF,
        .topbar   = 0x00101010,
        .cursor_fg= 0x0000FF00,
        .cursor_bg= 0x00000000,
    },
    /* 2: Midnight — deep blue + cyan */
    {
        .name     = "Midnight",
        .bg       = 0x00281008,
        .panel    = 0x00381810,
        .panel_hi = 0x00482818,
        .accent   = 0x00FFCC00, /* cyan */
        .text     = 0x00F0E8E0,
        .muted    = 0x00A09080,
        .dim      = 0x00483020,
        .body     = 0x00301410,
        .ok       = 0x0060DC40,
        .err      = 0x004040FF,
        .topbar   = 0x00301008,
        .cursor_fg= 0x00FFFFFF,
        .cursor_bg= 0x00000000,
    },
    /* 3: Slate — gray + yellow (the VM default look) */
    {
        .name     = "Slate",
        .bg       = 0x00201C18,
        .panel    = 0x00302A25,
        .panel_hi = 0x00403A35,
        .accent   = 0x0000DDFF, /* yellow (BGR) */
        .text     = 0x00F0E8E0,
        .muted    = 0x00908880,
        .dim      = 0x00605850,
        .body     = 0x00282420,
        .ok       = 0x0060DC40,
        .err      = 0x004848FF,
        .topbar   = 0x00282420,
        .cursor_fg= 0x00FFFFFF,
        .cursor_bg= 0x00000000,
    },
    /* 4: Arctic — dark blue-gray + ice blue */
    {
        .name     = "Arctic",
        .bg       = 0x00241C14,
        .panel    = 0x00342C20,
        .panel_hi = 0x00443C30,
        .accent   = 0x00FFD060, /* light blue (BGR) */
        .text     = 0x00F8F0E8,
        .muted    = 0x00A09888,
        .dim      = 0x00504030,
        .body     = 0x002C2418,
        .ok       = 0x0080E060,
        .err      = 0x006060FF,
        .topbar   = 0x002C241C,
        .cursor_fg= 0x00FFFFFF,
        .cursor_bg= 0x00000000,
    },
    /* 5: Blood — dark + red accent */
    {
        .name     = "Blood",
        .bg       = 0x00100808,
        .panel    = 0x00201010,
        .panel_hi = 0x00301818,
        .accent   = 0x000030FF, /* red (BGR) */
        .text     = 0x00E0D8D0,
        .muted    = 0x00806060,
        .dim      = 0x00403030,
        .body     = 0x00180C0C,
        .ok       = 0x0040C040,
        .err      = 0x000020E0,
        .topbar   = 0x00180C08,
        .cursor_fg= 0x00FFFFFF,
        .cursor_bg= 0x00000000,
    },
};

static int current_theme = 0;

void theme_init(void) { current_theme = 0; }
void theme_set(int index) { if (index >= 0 && index < THEME_COUNT) current_theme = index; }
int  theme_get(void) { return current_theme; }
int  theme_count(void) { return THEME_COUNT; }
const theme_t *theme_current(void) { return &themes[current_theme]; }
const char *theme_name(int index) { return (index >= 0 && index < THEME_COUNT) ? themes[index].name : ""; }
