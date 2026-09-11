#include "mouse.h"
#include "../kernel/io.h"
#include "../kernel/idt.h"

#define MOUSE_PORT   0x60
#define MOUSE_STATUS 0x64
#define MOUSE_CMD    0x64

static int mouse_x = 0;
static int mouse_y = 0;
static int mouse_buttons = 0;
static int bound_w = 320;
static int bound_h = 200;
static uint8_t mouse_cycle = 0;
static int8_t  mouse_bytes[3];

static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(MOUSE_STATUS) & 0x02)) return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(MOUSE_STATUS) & 0x01) return;
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait_write();
    outb(MOUSE_CMD, 0xD4);
    mouse_wait_write();
    outb(MOUSE_PORT, val);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(MOUSE_PORT);
}

static void mouse_handler(registers_t *regs) {
    (void)regs;
    uint8_t status = inb(MOUSE_STATUS);
    if (!(status & 0x20)) return;

    int8_t data = (int8_t)inb(MOUSE_PORT);

    switch (mouse_cycle) {
    case 0:
        mouse_bytes[0] = data;
        if (data & 0x08) mouse_cycle = 1;
        break;
    case 1:
        mouse_bytes[1] = data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_bytes[2] = data;
        mouse_cycle = 0;

        mouse_buttons = mouse_bytes[0] & 0x07;
        mouse_x += mouse_bytes[1];
        mouse_y -= mouse_bytes[2];

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= bound_w) mouse_x = bound_w - 1;
        if (mouse_y >= bound_h) mouse_y = bound_h - 1;
        break;
    }
}

void mouse_init(void) {
    mouse_wait_write();
    outb(MOUSE_CMD, 0xA8);

    mouse_wait_write();
    outb(MOUSE_CMD, 0x20);
    mouse_wait_read();
    uint8_t status = inb(MOUSE_PORT) | 0x02;
    status &= ~0x20;
    mouse_wait_write();
    outb(MOUSE_CMD, 0x60);
    mouse_wait_write();
    outb(MOUSE_PORT, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF3);
    mouse_read();
    mouse_write(100);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();

    register_interrupt_handler(44, mouse_handler);

    mouse_x = bound_w / 2;
    mouse_y = bound_h / 2;
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }
void mouse_set_bounds(int w, int h) { bound_w = w; bound_h = h; }
