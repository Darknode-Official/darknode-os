#ifndef SYSCALL_H
#define SYSCALL_H
#include "../include/types.h"
#include "idt.h"

#define SYS_EXIT    0
#define SYS_READ    1
#define SYS_WRITE   2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_GETPID  5
#define SYS_SLEEP   6
#define SYS_EXEC    7
#define SYS_FORK    8
#define SYS_WAIT    9
#define SYS_SBRK   10
#define SYS_MAX    11

void syscall_init(void);

#endif
