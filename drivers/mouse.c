#include "mouse.h"
#include "../kernel/io.h"
#include "../kernel/idt.h"
#include "../kernel/serial.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

static volatile int mouse_x = 0;
static volatile int mouse_y = 0;
static volatile int mouse_buttons = 0;
static int bound_w = 320;
static int bound_h = 200;

static uint8_t packet[3];
static volatile int packet_idx = 0;

static void wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(PS2_STATUS) & 0x02)) return;
}

static void wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(PS2_STATUS) & 0x01) return;
}

static void cmd_controller(uint8_t cmd) {
    wait_write();
    outb(PS2_CMD, cmd);
}

static void cmd_mouse(uint8_t val) {
    wait_write();
    outb(PS2_CMD, 0xD4);
    wait_write();
    outb(PS2_DATA, val);
}

static uint8_t read_data(void) {
    wait_read();
    return inb(PS2_DATA);
}

static volatile int debug_irq_count = 0;
static volatile int debug_poll_count = 0;
static volatile int debug_packet_count = 0;

static void apply_packet(void) {
    int8_t dx = (int8_t)packet[1];
    int8_t dy = (int8_t)packet[2];

    /* Check overflow bits — discard if set */
    if (packet[0] & 0xC0) return;

    mouse_buttons = packet[0] & 0x07;

    /* Apply sign extension from flags byte */
    int sx = dx; if (packet[0] & 0x10) sx |= 0xFFFFFF00;
    int sy = dy; if (packet[0] & 0x20) sy |= 0xFFFFFF00;

    mouse_x += sx;
    mouse_y -= sy;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= bound_w) mouse_x = bound_w - 1;
    if (mouse_y >= bound_h) mouse_y = bound_h - 1;
}

static void feed_byte(uint8_t data) {
    if (packet_idx == 0) {
        /* Byte 0 must have bit 3 set (always-1 bit in PS/2 protocol) */
        if (!(data & 0x08)) return;
        packet[0] = data;
        packet_idx = 1;
    } else if (packet_idx == 1) {
        packet[1] = data;
        packet_idx = 2;
    } else {
        packet[2] = data;
        packet_idx = 0;
        debug_packet_count++;
        apply_packet();
    }
}

/* IRQ12 handler */
static void mouse_handler(registers_t *regs) {
    (void)regs;
    debug_irq_count++;
    uint8_t data = inb(PS2_DATA);
    feed_byte(data);
}

/* Polling fallback — call every frame from GUI loop */
void mouse_poll(void) {
    int max_reads = 10;
    while (max_reads-- > 0) {
        uint8_t status = inb(PS2_STATUS);
        if (!(status & 0x01)) break;

        uint8_t data = inb(PS2_DATA);

        if (status & 0x20) {
            debug_poll_count++;
            feed_byte(data);
        }
    }
}

int mouse_debug_irqs(void) { return debug_irq_count; }
int mouse_debug_polls(void) { return debug_poll_count; }
int mouse_debug_packets(void) { return debug_packet_count; }

void mouse_init(void) {
    /* Flush stale data */
    while (inb(PS2_STATUS) & 0x01) inb(PS2_DATA);

    /* Disable both ports */
    cmd_controller(0xAD);
    cmd_controller(0xA7);

    /* Flush again */
    while (inb(PS2_STATUS) & 0x01) inb(PS2_DATA);

    /* Enable aux port */
    cmd_controller(0xA8);

    /* Read controller config */
    cmd_controller(0x20);
    uint8_t cfg = read_data();
    cfg |= 0x02;     /* enable IRQ12 */
    cfg |= 0x01;     /* enable IRQ1 (keyboard) */
    cfg &= ~0x20;    /* enable aux clock */
    cfg &= ~0x10;    /* enable keyboard clock */
    cmd_controller(0x60);
    wait_write();
    outb(PS2_DATA, cfg);

    /* Self-test controller */
    cmd_controller(0xAA);
    read_data();  /* should be 0x55 */

    /* Re-enable aux port (self-test may disable it) */
    cmd_controller(0xA8);

    /* Test aux port */
    cmd_controller(0xA9);
    read_data();  /* should be 0x00 */

    /* Reset mouse */
    cmd_mouse(0xFF);
    read_data();  /* ACK */
    read_data();  /* 0xAA self-test pass */
    read_data();  /* 0x00 device ID */

    /* Use default settings */
    cmd_mouse(0xF6);
    read_data();

    /* Set sample rate 100 */
    cmd_mouse(0xF3); read_data();
    cmd_mouse(100);  read_data();

    /* Set resolution (2 = 4 count/mm) */
    cmd_mouse(0xE8); read_data();
    cmd_mouse(0x02); read_data();

    /* Set scaling 1:1 */
    cmd_mouse(0xE6); read_data();

    /* Enable data reporting */
    cmd_mouse(0xF4);
    read_data();

    /* Re-enable keyboard port */
    cmd_controller(0xAE);

    /* Flush any data generated during init */
    while (inb(PS2_STATUS) & 0x01) inb(PS2_DATA);

    /* Register IRQ12 handler */
    register_interrupt_handler(44, mouse_handler);

    /* Unmask IRQ12 on slave PIC explicitly */
    uint8_t mask = inb(0xA1);
    mask &= ~(1 << 4);  /* IRQ12 = slave IRQ4 */
    outb(0xA1, mask);

    /* Also ensure cascade (IRQ2) is unmasked on master */
    mask = inb(0x21);
    mask &= ~(1 << 2);
    outb(0x21, mask);

    mouse_x = bound_w / 2;
    mouse_y = bound_h / 2;
    packet_idx = 0;
}

void mouse_usb_update(int dx, int dy, int buttons) {
    mouse_buttons = buttons & 0x07;
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= bound_w) mouse_x = bound_w - 1;
    if (mouse_y >= bound_h) mouse_y = bound_h - 1;
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }
void mouse_set_bounds(int w, int h) { bound_w = w; bound_h = h; }
