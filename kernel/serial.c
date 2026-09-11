#include "serial.h"
#include "io.h"

void serial_init(void) {
    outb(COM1 + 1, 0x00);  /* disable interrupts */
    outb(COM1 + 3, 0x80);  /* enable DLAB */
    outb(COM1 + 0, 0x03);  /* baud divisor low: 38400 */
    outb(COM1 + 1, 0x00);  /* baud divisor high */
    outb(COM1 + 3, 0x03);  /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);  /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static int serial_ready(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_putchar(char c) {
    while (!serial_ready());
    outb(COM1, c);
}

void serial_write(const char *s) {
    while (*s) { if (*s == '\n') serial_putchar('\r'); serial_putchar(*s++); }
}
