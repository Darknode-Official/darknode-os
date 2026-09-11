#include "timer.h"
#include "idt.h"
#include "io.h"
#include "console.h"

static volatile uint32_t ticks = 0;
static uint32_t timer_freq = 0;

static void timer_callback(registers_t *regs) {
    (void)regs;
    ticks++;
}

void timer_init(uint32_t freq) {
    timer_freq = freq;
    register_interrupt_handler(32, timer_callback);
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint32_t timer_get_ticks(void) { return ticks; }

void timer_wait(uint32_t ms) {
    uint32_t end = ticks + (ms * timer_freq / 1000);
    while (ticks < end) __asm__ volatile("hlt");
}
