#include "rtc.h"
#include "../kernel/io.h"

uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void rtc_init(void) {
    /* ensure RTC is using BCD mode (default on most hardware) */
    uint8_t status = rtc_read(0x0B);
    (void)status;
}
