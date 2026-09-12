#include "keyboard.h"
#include "idt.h"
#include "io.h"

#define KB_BUF_SIZE 256

static char kb_buf[KB_BUF_SIZE];
static volatile int kb_head = 0, kb_tail = 0;
static bool shift_pressed = false;

static const char scancode_ascii[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};

static const char scancode_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0,0,0
};

static void keyboard_callback(registers_t *regs) {
    (void)regs;
    /* Check if data is actually from keyboard (bit 5 clear) not mouse (bit 5 set) */
    uint8_t status = inb(0x64);
    if (status & 0x20) return;  /* mouse data — leave it for the mouse driver */

    uint8_t sc = inb(0x60);

    if (sc == 0x2A || sc == 0x36) { shift_pressed = true; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = false; return; }
    if (sc & 0x80) return;

    char c = shift_pressed ? scancode_shift[sc] : scancode_ascii[sc];
    if (c == 0) return;

    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

void keyboard_push_char(char c) {
    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback);
}

bool keyboard_has_key(void) {
    return kb_head != kb_tail;
}

char keyboard_getchar(void) {
    if (!keyboard_has_key()) return 0;
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

char keyboard_getchar_blocking(void) {
    while (!keyboard_has_key()) __asm__ volatile("hlt");
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

/* Return raw scancode (not translated to ASCII) — for arrow keys, F-keys etc */
static volatile uint8_t last_scancode = 0;
uint8_t keyboard_last_scancode(void) {
    uint8_t sc = last_scancode;
    last_scancode = 0;
    return sc;
}
