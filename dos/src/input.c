/*
 * input.c — Wejście: klawiatura (INT 9 ISR), joystick Game Port $201, mysz serial
 * Gruniożerca DOS port, 2024
 *
 * Port z nes/Sys/KEYPAD.asm:
 *  - Edge-detection (naciśnięcie vs trzymanie) identycznie jak p1button w NES
 *  - keys_pressed[] = nowe naciśnięcia w tej klatce (odpowiednik p1button)
 *  - keys_held[] = stan ciągły (odpowiednik gmpad)
 */
#include "input.h"
#include "dos_compat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------- Stan klawiatury ----------------------------------------------- */
uint8_t keys_held[KEY_COUNT];
uint8_t keys_pressed[KEY_COUNT];
uint8_t keys_released[KEY_COUNT];
uint8_t action_key[ACT_COUNT] = {
    SC_LEFT,   /* ACT_LEFT   */
    SC_RIGHT,  /* ACT_RIGHT  */
    SC_Z,      /* ACT_ACTION */
    SC_ENTER,  /* ACT_START  */
    SC_TAB     /* ACT_SELECT */
};

/* Poprzedni stan do obliczenia edge-detection */
static uint8_t keys_prev[KEY_COUNT];

/* Flaga aktywnych urządzeń */
static uint8_t dev_enabled[3] = { 1, 1, 1 }; /* KB, JOY, MOUSE */

/* ---------- Stan joysticka ------------------------------------------------ */
JoystickState joy = {
    .x = 0, .y = 0,
    .btn1 = 0, .btn2 = 0,
    .present = 0,
    .x_min = 10, .x_max = 240, .x_center = 125,
    .y_min = 10, .y_max = 240, .y_center = 125,
    .dead_zone = 20
};

/* ---------- Stan myszy ----------------------------------------------------- */
MouseState mouse = {
    .x = 0, .y = 0,
    .btn_left = 0, .btn_right = 0,
    .present = 0,
    .com_port = 0x3F8  /* COM1 domyślnie */
};

/* ---------- Raw key buffer (ring) ----------------------------------------- */
#define RAW_BUF_SIZE 32
static volatile uint8_t raw_buf[RAW_BUF_SIZE];
static volatile uint8_t raw_head = 0;
static volatile uint8_t raw_tail = 0;

#ifdef DOS_BUILD
#include <dpmi.h>
#include <go32.h>
#include <pc.h>

static _go32_dpmi_seginfo old_kb_isr, new_kb_isr;
static _go32_dpmi_seginfo old_com_isr, new_com_isr;
static uint8_t  com_irq  = 4;   /* COM1=IRQ4, COM2=IRQ3 */
static uint8_t  com_mask = 0;   /* maska PIC dla COM */

/* ===================== ISR klawiatury ===================================== */
static void keyboard_isr(void) {
    uint8_t key = inportb(0x60); /* odczyt klawisza */

    /* Potwierdzenie kontrolerowi klawiatury */
    uint8_t ctrl = inportb(0x61);
    outportb(0x61, ctrl | 0x80);
    outportb(0x61, ctrl & 0x7F);

    /* Dodaj do ring buffera */
    uint8_t next = (raw_tail + 1) % RAW_BUF_SIZE;
    if (next != raw_head) {
        raw_buf[raw_tail] = key;
        raw_tail = next;
    }

    outportb(0x20, 0x20); /* EOI do PIC */
}

/* ===================== ISR myszy serial (COM UART) ======================== */
#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8

static uint8_t  mouse_buf[3];   /* 3-bajtowy pakiet Microsoft Mouse */
static uint8_t  mouse_idx = 0;

static void com_isr(void) {
    /* Czytaj bajty dostępne w FIFO UART */
    while (inportb(mouse.com_port + 5) & 0x01) {
        uint8_t b = inportb(mouse.com_port);

        /* Protokół Microsoft Mouse Serial:
           Bajt 0: bit7=1, bit6=LB, bit5=RB, bit4-3=Y[8:7], bit2-1=X[8:7]
           Bajt 1: X delta (signed, bity 7:2 z bajtu 0)
           Bajt 2: Y delta (signed, bity 5:4 z bajtu 0) */
        if (b & 0x40) {
            /* Nowy pakiet — synchronizacja na bicie 6 */
            mouse_idx = 0;
        }
        if (mouse_idx < 3) {
            mouse_buf[mouse_idx++] = b;
        }
        if (mouse_idx == 3) {
            /* Kompletny pakiet 3-bajtowy */
            int8_t dx = (int8_t)(((mouse_buf[0] & 0x03) << 6) | (mouse_buf[1] & 0x3F));
            int8_t dy = (int8_t)(((mouse_buf[0] & 0x0C) << 4) | (mouse_buf[2] & 0x3F));
            mouse.x        += dx;
            mouse.y        += dy;
            mouse.btn_left  = (mouse_buf[0] >> 5) & 0x01;
            mouse.btn_right = (mouse_buf[0] >> 4) & 0x01;
            mouse_idx = 0;
        }
    }
    outportb(0x20, 0x20); /* EOI */
    if (com_irq >= 8)
        outportb(0xA0, 0x20); /* EOI slave PIC */
}

/* ===================== Inicjalizacja klawiatury =========================== */
static void kb_init(void) {
    _go32_dpmi_get_protected_mode_interrupt_vector(0x09, &old_kb_isr);
    new_kb_isr.pm_selector = _go32_my_cs();
    new_kb_isr.pm_offset   = (uint32_t)keyboard_isr;
    _go32_dpmi_allocate_iret_wrapper(&new_kb_isr);
    _go32_dpmi_set_protected_mode_interrupt_vector(0x09, &new_kb_isr);
}

static void kb_shutdown(void) {
    _go32_dpmi_set_protected_mode_interrupt_vector(0x09, &old_kb_isr);
    _go32_dpmi_free_iret_wrapper(&new_kb_isr);
}

/* ===================== Joystick Game Port ($201) ========================== */
/* Pomiar wartości osi — mierzy czas rozładowania kondensatora RC */
static uint16_t joy_read_axis(uint8_t mask) {
    uint16_t count = 0;
    CLI();
    outportb(0x201, 0x00);  /* wyzwól pomiar */
    STI();
    /* Zliczaj cykle do opadnięcia bitu */
    while ((inportb(0x201) & mask) && count < 0xFFFF)
        count++;
    return count;
}

static int joy_probe(void) {
    /* Joystick obecny jeśli bity 0..3 w porcie $201 reagują na wyzwolenie */
    outportb(0x201, 0x00);
    uint16_t t = 0;
    while ((inportb(0x201) & 0x01) && t < 500) t++;
    return (t > 0 && t < 490);
}

/* ===================== Inicjalizacja UART (mysz serial) =================== */
int input_init_mouse(uint16_t com_port) {
    mouse.com_port = com_port;
    uint8_t irq_line = (com_port == COM1_PORT) ? 4 : 3;
    uint8_t int_vec  = 0x0B + (irq_line == 3 ? 0 : 1); /* INT 0x0C=COM1, 0x0B=COM2 */

    /* Skonfiguruj UART: 1200 baud, 7N1 (Microsoft Mouse) */
    outportb(com_port + 3, 0x80);        /* DLAB=1 */
    outportb(com_port + 0, 96);          /* 1200 baud divisor LSB (1843200/16/1200=96) */
    outportb(com_port + 1, 0x00);        /* MSB */
    outportb(com_port + 3, 0x02);        /* 7-bit, no parity, 1 stop, DLAB=0 */
    outportb(com_port + 2, 0xC7);        /* Enable FIFO (16550) */
    outportb(com_port + 1, 0x01);        /* Interrupt on data received */
    outportb(com_port + 4, 0x0B);        /* DTR+RTS+OUT2 (zasilanie i INT) */

    /* Odczekaj reset (mysz wyśle 'M' po zasileniu przez DTR) */
    uint16_t timeout = 2000;
    while (timeout-- && !(inportb(com_port + 5) & 0x01)) ;
    if (inportb(com_port) != 'M') {
        /* Brak odpowiedzi — brak myszy */
        outportb(com_port + 1, 0x00);
        return 0;
    }

    /* Zainstaluj ISR dla COM */
    com_irq = irq_line;
    com_mask = (uint8_t)(1 << irq_line);
    _go32_dpmi_get_protected_mode_interrupt_vector(int_vec, &old_com_isr);
    new_com_isr.pm_selector = _go32_my_cs();
    new_com_isr.pm_offset   = (uint32_t)com_isr;
    _go32_dpmi_allocate_iret_wrapper(&new_com_isr);
    _go32_dpmi_set_protected_mode_interrupt_vector(int_vec, &new_com_isr);

    /* Odblokuj IRQ w PIC */
    CLI();
    outportb(0x21, inportb(0x21) & ~com_mask);
    STI();

    mouse.present = 1;
    return 1;
}

#endif /* DOS_BUILD */

/* =========================================================================== */

void input_init(void) {
    memset(keys_held,     0, sizeof(keys_held));
    memset(keys_pressed,  0, sizeof(keys_pressed));
    memset(keys_released, 0, sizeof(keys_released));
    memset(keys_prev,     0, sizeof(keys_prev));
    raw_head = raw_tail = 0;

#ifdef DOS_BUILD
    /* Klawiatura */
    kb_init();

    /* Joystick */
    if (dev_enabled[INPUT_DEV_JOYSTICK] && joy_probe()) {
        joy.present = 1;
        /* Prosta kalibracja startowa — użyj wartości domyślnych */
    }

    /* Mysz serial — spróbuj COM1, potem COM2 */
    if (dev_enabled[INPUT_DEV_MOUSE]) {
        if (!input_init_mouse(0x3F8))
            input_init_mouse(0x2F8);
    }
#endif
}

void input_shutdown(void) {
#ifdef DOS_BUILD
    kb_shutdown();

    if (mouse.present) {
        /* Wyłącz przerwania UART */
        outportb(mouse.com_port + 1, 0x00);
        outportb(mouse.com_port + 4, 0x00);
        CLI();
        outportb(0x21, inportb(0x21) | com_mask);
        STI();
        uint8_t int_vec = (com_irq == 4) ? 0x0C : 0x0B;
        _go32_dpmi_set_protected_mode_interrupt_vector(int_vec, &old_com_isr);
        _go32_dpmi_free_iret_wrapper(&new_com_isr);
    }
#endif
}

void input_update(void) {
    /* --- Klawiatura: przetwórz ring buffer scancodes ---------------------- */
    memcpy(keys_prev, keys_held, KEY_COUNT);

#ifdef DOS_BUILD
    /* Opróżnij ring buffer */
    while (raw_head != raw_tail) {
        uint8_t scan = raw_buf[raw_head];
        raw_head = (raw_head + 1) % RAW_BUF_SIZE;

        if (scan & 0x80) {
            /* Key up */
            keys_held[scan & 0x7F] = 0;
        } else {
            /* Key down */
            if (scan < KEY_COUNT)
                keys_held[scan] = 1;
        }
    }
#endif

    /* Edge detection */
    for (int i = 0; i < KEY_COUNT; i++) {
        keys_pressed[i]  = keys_held[i] & ~keys_prev[i];
        keys_released[i] = ~keys_held[i] & keys_prev[i];
    }

    /* --- Joystick --------------------------------------------------------- */
#ifdef DOS_BUILD
    if (joy.present && dev_enabled[INPUT_DEV_JOYSTICK]) {
        uint16_t rx = joy_read_axis(0x01);
        uint16_t ry = joy_read_axis(0x02);
        uint8_t  pb = inportb(0x201);

        /* Normalizuj oś X: min..center..max → -127..0..+127 */
        int16_t nx = (int16_t)rx - joy.x_center;
        if (nx >  127) nx =  127;
        if (nx < -127) nx = -127;
        joy.x = (abs(nx) > (int)joy.dead_zone) ? nx : 0;

        int16_t ny = (int16_t)ry - joy.y_center;
        if (ny >  127) ny =  127;
        if (ny < -127) ny = -127;
        joy.y = (abs(ny) > (int)joy.dead_zone) ? ny : 0;

        joy.btn1 = !((pb >> 4) & 1); /* bit 4 = przycisk 1, aktywny LOW */
        joy.btn2 = !((pb >> 5) & 1); /* bit 5 = przycisk 2 */
    }
#endif
}

int input_held(GameAction act) {
    if (!dev_enabled[INPUT_DEV_KEYBOARD]) goto check_joy;
    if (keys_held[action_key[act]]) return 1;

check_joy:
    if (!joy.present || !dev_enabled[INPUT_DEV_JOYSTICK]) return 0;
    switch (act) {
        case ACT_LEFT:   return (joy.x < -20);
        case ACT_RIGHT:  return (joy.x >  20);
        case ACT_ACTION: return joy.btn1;
        case ACT_START:  return joy.btn2;
        default:         return 0;
    }
}

int input_pressed(GameAction act) {
    if (dev_enabled[INPUT_DEV_KEYBOARD] && keys_pressed[action_key[act]])
        return 1;
    /* Joystick nie ma edge-detection — pominięte dla uproszczenia */
    return 0;
}

void input_calibrate_joystick(void) {
#ifdef DOS_BUILD
    if (!joy.present) return;

    /* Kalibracja: użytkownik trzyma joy w skrajnych pozycjach */
    /* Krok 1: środek */
    /* (zakładamy że wywołanie następuje z UI SETUP) */
    uint16_t cx = joy_read_axis(0x01);
    uint16_t cy = joy_read_axis(0x02);
    joy.x_center = (int16_t)cx;
    joy.y_center = (int16_t)cy;
    joy.x_min = cx - 100;
    joy.x_max = cx + 100;
    joy.y_min = cy - 100;
    joy.y_max = cy + 100;
#endif
}

void input_disable_device(InputDevice dev) {
    if ((unsigned)dev < 3) dev_enabled[dev] = 0;
}

void input_enable_device(InputDevice dev) {
    if ((unsigned)dev < 3) dev_enabled[dev] = 1;
}
