#ifndef SERIAL_H
#define SERIAL_H
#include "../include/types.h"

#define COM1 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *s);

#endif
