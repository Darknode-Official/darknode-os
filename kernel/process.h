#ifndef PROCESS_H
#define PROCESS_H
#include "../include/types.h"
#include "idt.h"

#define PROC_NAME_MAX   64
#define PROC_MAX        64
#define KERNEL_STACK_SZ 4096

typedef enum {
    PROC_CREATED,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
} proc_state_t;

typedef struct process {
    uint32_t      pid;
    char          name[PROC_NAME_MAX];
    proc_state_t  state;
    uint32_t      priority;
    uint32_t      esp;
    uint32_t      ebp;
    uint32_t      eip;
    uint32_t     *kernel_stack;
    struct process *next;
} process_t;

void       process_init(void);
process_t *process_create(const char *name, void (*entry)(void));
void       process_kill(uint32_t pid);
process_t *process_current(void);
process_t *process_list(void);
uint32_t   process_count(void);
void       schedule(registers_t *regs);

#endif
