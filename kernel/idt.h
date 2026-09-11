#ifndef IDT_H
#define IDT_H
#include "../include/types.h"

typedef struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} PACKED idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} PACKED idt_ptr_t;

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

typedef void (*isr_handler_t)(registers_t *);
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif
