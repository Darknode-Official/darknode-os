; GDT flush — reload segment registers after loading a new GDT.
bits 32
section .text
global gdt_flush
extern gdt_ptr

gdt_flush:
    lgdt [gdt_ptr]
    mov ax, 0x10    ; kernel data segment (index 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush ; far jump to kernel code segment (index 1)
.flush:
    ret
