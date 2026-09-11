#ifndef RTC_H
#define RTC_H
#include "../include/types.h"

uint8_t rtc_read(uint8_t reg);
void    rtc_init(void);

#endif
