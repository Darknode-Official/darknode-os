#include "process.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "../include/darknode.h"

static process_t *proc_table[PROC_MAX];
static uint32_t   proc_count = 0;
static uint32_t   next_pid = 0;
static process_t *current_proc = NULL;
static process_t *ready_head = NULL;

static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

void process_init(void) {
    memset(proc_table, 0, sizeof(proc_table));
    proc_count = 0;
    next_pid = 0;
    current_proc = NULL;
    ready_head = NULL;

    /* PID 0: idle process */
    process_create("idle", idle_task);
    proc_table[0]->state = PROC_RUNNING;
    current_proc = proc_table[0];
}

process_t *process_create(const char *name, void (*entry)(void)) {
    if (proc_count >= PROC_MAX) return NULL;

    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) return NULL;
    memset(proc, 0, sizeof(process_t));

    proc->pid      = next_pid++;
    proc->state    = PROC_READY;
    proc->priority = 1;
    strncpy(proc->name, name, PROC_NAME_MAX - 1);

    /* Allocate kernel stack */
    proc->kernel_stack = (uint32_t *)kmalloc(KERNEL_STACK_SZ);
    if (!proc->kernel_stack) {
        kfree(proc);
        return NULL;
    }
    memset(proc->kernel_stack, 0, KERNEL_STACK_SZ);

    /* Set up initial stack frame so schedule() can switch to it.
       Stack grows downward: push fake context at the top. */
    uint32_t *sp = (uint32_t *)((uint32_t)proc->kernel_stack + KERNEL_STACK_SZ);

    /* Fake return address for the entry point */
    *(--sp) = (uint32_t)entry;     /* EIP: where to start executing */
    *(--sp) = 0;                   /* EBP */
    *(--sp) = 0;                   /* EBX */
    *(--sp) = 0;                   /* ESI */
    *(--sp) = 0;                   /* EDI */
    *(--sp) = 0x200;               /* EFLAGS (IF=1, interrupts enabled) */

    proc->esp = (uint32_t)sp;
    proc->ebp = 0;
    proc->eip = (uint32_t)entry;

    proc_table[proc->pid] = proc;
    proc_count++;

    /* Add to ready queue */
    if (!ready_head) {
        ready_head = proc;
        proc->next = proc;
    } else {
        process_t *tail = ready_head;
        while (tail->next != ready_head) tail = tail->next;
        tail->next = proc;
        proc->next = ready_head;
    }

    return proc;
}

void process_kill(uint32_t pid) {
    if (pid >= PROC_MAX || !proc_table[pid]) return;
    process_t *proc = proc_table[pid];

    proc->state = PROC_ZOMBIE;

    /* Remove from ready queue */
    if (proc == ready_head && proc->next == proc) {
        ready_head = NULL;
    } else {
        process_t *prev = ready_head;
        while (prev->next != proc) prev = prev->next;
        prev->next = proc->next;
        if (proc == ready_head) ready_head = proc->next;
    }

    if (proc->kernel_stack) kfree(proc->kernel_stack);
    kfree(proc);
    proc_table[pid] = NULL;
    proc_count--;
}

process_t *process_current(void) {
    return current_proc;
}

process_t *process_list(void) {
    return ready_head;
}

uint32_t process_count(void) {
    return proc_count;
}

/* Round-robin scheduler — called from timer IRQ handler.
   Saves current ESP into current process, picks the next READY process,
   restores its ESP. Context switch happens via the modified stack pointer. */
void schedule(registers_t *regs) {
    if (!ready_head || !current_proc) return;

    /* Save current context */
    current_proc->esp = regs->esp;
    current_proc->ebp = regs->ebp;
    if (current_proc->state == PROC_RUNNING)
        current_proc->state = PROC_READY;

    /* Find next READY process (round-robin) */
    process_t *next = current_proc->next;
    uint32_t limit = proc_count + 1;
    while (next->state != PROC_READY && limit > 0) {
        next = next->next;
        limit--;
    }

    if (next->state != PROC_READY) {
        /* No ready process; run idle */
        next = proc_table[0];
    }

    next->state = PROC_RUNNING;
    current_proc = next;

    /* Restore context */
    regs->esp = current_proc->esp;
    regs->ebp = current_proc->ebp;
}
