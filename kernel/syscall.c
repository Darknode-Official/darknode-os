#include "syscall.h"
#include "console.h"
#include "process.h"
#include "timer.h"
#include "heap.h"
#include "../fs/vfs.h"
#include "../include/darknode.h"

static void sys_exit(registers_t *regs) {
    process_t *proc = process_current();
    if (proc && proc->pid != 0) {
        process_kill(proc->pid);
    }
    (void)regs;
}

static void sys_read(registers_t *regs) {
    /* ebx = fd (ignored for now, reads from console)
       ecx = buffer pointer
       edx = count */
    uint8_t *buf = (uint8_t *)regs->ecx;
    uint32_t size = regs->edx;
    vfs_node_t *con = vfs_resolve("/dev/console");
    if (con) {
        regs->eax = vfs_read(con, 0, size, buf);
    } else {
        regs->eax = 0;
    }
}

static void sys_write(registers_t *regs) {
    /* ebx = fd (1=stdout, 2=stderr → both go to console)
       ecx = buffer pointer
       edx = count */
    const uint8_t *buf = (const uint8_t *)regs->ecx;
    uint32_t size = regs->edx;
    for (uint32_t i = 0; i < size; i++)
        console_putchar((char)buf[i]);
    regs->eax = size;
}

static void sys_open(registers_t *regs) {
    const char *path = (const char *)regs->ebx;
    vfs_node_t *node = vfs_resolve(path);
    if (node) {
        vfs_open(node);
        regs->eax = (uint32_t)node;
    } else {
        regs->eax = 0;
    }
}

static void sys_close(registers_t *regs) {
    vfs_node_t *node = (vfs_node_t *)regs->ebx;
    if (node) vfs_close(node);
    regs->eax = 0;
}

static void sys_getpid(registers_t *regs) {
    process_t *proc = process_current();
    regs->eax = proc ? proc->pid : 0;
}

static void sys_sleep(registers_t *regs) {
    uint32_t ms = regs->ebx;
    timer_wait(ms);
    regs->eax = 0;
}

static void sys_exec(registers_t *regs) {
    /* Stub: not fully implemented (would need ELF loader) */
    (void)regs;
    regs->eax = (uint32_t)-1;
}

static void sys_fork(registers_t *regs) {
    /* Stub: fork not implemented yet */
    (void)regs;
    regs->eax = (uint32_t)-1;
}

static void sys_wait(registers_t *regs) {
    /* Stub */
    (void)regs;
    regs->eax = 0;
}

static void sys_sbrk(registers_t *regs) {
    uint32_t increment = regs->ebx;
    void *ptr = kmalloc(increment);
    regs->eax = (uint32_t)ptr;
}

typedef void (*syscall_fn_t)(registers_t *);

static syscall_fn_t syscall_table[SYS_MAX] = {
    [SYS_EXIT]   = sys_exit,
    [SYS_READ]   = sys_read,
    [SYS_WRITE]  = sys_write,
    [SYS_OPEN]   = sys_open,
    [SYS_CLOSE]  = sys_close,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP]  = sys_sleep,
    [SYS_EXEC]   = sys_exec,
    [SYS_FORK]   = sys_fork,
    [SYS_WAIT]   = sys_wait,
    [SYS_SBRK]   = sys_sbrk,
};

static void syscall_handler(registers_t *regs) {
    uint32_t num = regs->eax;
    if (num < SYS_MAX && syscall_table[num]) {
        syscall_table[num](regs);
    } else {
        regs->eax = (uint32_t)-1;
    }
}

void syscall_init(void) {
    register_interrupt_handler(0x80, syscall_handler);
}
