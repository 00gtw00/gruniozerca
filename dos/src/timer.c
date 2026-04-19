/*
 * timer.c — Timer 60 Hz przez PIT canal 0 (INT 8)
 * Gruniożerca DOS port, 2024
 *
 * Port z nes/Sys/general.asm (waitframe) i nes/Sys/nmi.asm (global tick).
 * PIT przeprogramowany z domyślnych 18.2 Hz na 60 Hz.
 * Oryginał przywracany przy shutdown (wymagane przez DOS).
 */
#include "timer.h"
#include "dos_compat.h"
#include <stdint.h>

/* ---------- Globalne ---------------------------------------------------- */
volatile uint32_t tick_count = 0;

#ifdef DOS_BUILD
#include <dpmi.h>
#include <go32.h>
#include <pc.h>

static _go32_dpmi_seginfo old_timer_isr;
static _go32_dpmi_seginfo new_timer_isr;
static uint32_t           last_tick = 0;

/* ISR — wywoływana przez PIT ~60 razy na sekundę.
   Nie chainujemy BIOS (zegar DOS może dryfować, nie szkodzi dla gry). */
static void timer_isr(void) {
    tick_count++;
    outportb(0x20, 0x20); /* EOI do PIC master */
}

void timer_init(void) {
    /* Zachowaj oryginalny handler INT 8 */
    _go32_dpmi_get_protected_mode_interrupt_vector(0x08, &old_timer_isr);

    /* Zainstaluj nowy handler z wrapperem IRET */
    new_timer_isr.pm_selector = _go32_my_cs();
    new_timer_isr.pm_offset   = (uint32_t)timer_isr;
    _go32_dpmi_allocate_iret_wrapper(&new_timer_isr);
    _go32_dpmi_set_protected_mode_interrupt_vector(0x08, &new_timer_isr);

    /* Przeprogramuj PIT canal 0 na TIMER_HZ */
    CLI();
    outportb(0x43, 0x36);                    /* canal 0, tryb 3 (square wave) */
    outportb(0x40, PIT_DIVISOR & 0xFF);      /* młodszy bajt dzielnika */
    outportb(0x40, (PIT_DIVISOR >> 8) & 0xFF); /* starszy bajt */
    STI();

    last_tick  = tick_count;
}

void timer_shutdown(void) {
    /* Przywróć oryginalny dzielnik PIT */
    CLI();
    outportb(0x43, 0x36);
    outportb(0x40, PIT_DIVISOR_ORIG & 0xFF);
    outportb(0x40, (PIT_DIVISOR_ORIG >> 8) & 0xFF);
    STI();

    /* Przywróć oryginalny handler */
    _go32_dpmi_set_protected_mode_interrupt_vector(0x08, &old_timer_isr);
    _go32_dpmi_free_iret_wrapper(&new_timer_isr);
}

void waitframe(void) {
    /* Czekaj aż tick_count się zmieni — odpowiednik NES waitframe */
    uint32_t target = last_tick + 1;
    while (tick_count < target)
        ; /* busy wait — CPU spędza czas w pętli, ale to ~1/60s */
    last_tick = tick_count;
}

uint32_t timer_ms(void) {
    return tick_count * (1000U / TIMER_HZ);
}

#else /* host build — wersja stub */

#include <time.h>

void timer_init(void)     {}
void timer_shutdown(void) {}

void waitframe(void) {
    struct timespec ts = { 0, 1000000000L / TIMER_HZ };
    nanosleep(&ts, NULL);
    tick_count++;
}

uint32_t timer_ms(void) {
    return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

#endif /* DOS_BUILD */
