; Darknode OS — boot entry point
; Sets up the stack and jumps to the C kernel.
bits 32
section .bss
align 16
stack_bottom:
    resb 16384           ; 16 KiB kernel stack
stack_top:

section .text
global _start
extern kmain

_start:
    mov esp, stack_top   ; set up the stack
    push ebx             ; multiboot2 info pointer
    push eax             ; multiboot2 magic number
    call kmain
.hang:
    cli
    hlt
    jmp .hang
