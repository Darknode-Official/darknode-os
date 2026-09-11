; Multiboot2 header — must appear in the first 32 KiB of the binary.
section .multiboot
align 8

mb2_start:
    dd 0xe85250d6            ; magic
    dd 0                     ; architecture: i386
    dd mb2_end - mb2_start   ; header length
    dd -(0xe85250d6 + 0 + (mb2_end - mb2_start)) ; checksum

    ; framebuffer tag — ask GRUB for a linear framebuffer
    align 8
    dw 5        ; type = framebuffer
    dw 0        ; flags (not optional)
    dd 20       ; size of this tag
    dd 1024     ; preferred width
    dd 768      ; preferred height
    dd 32       ; preferred bpp

    ; end tag
    align 8
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
mb2_end:
