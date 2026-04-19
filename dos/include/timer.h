/*
 * timer.h — Interfejs timera 60 Hz (PIT canal 0, INT 8)
 * Gruniożerca DOS port
 *
 * Przeprogramowuje PIT z 18.2 Hz → 60 Hz.
 * Co klatkę inkrementuje globalny licznik tick_count.
 * waitframe() blokuje do następnego tiku (synchronizacja logiki z VBlank).
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Docelowa częstotliwość (Hz) */
#define TIMER_HZ        60
/* Dzielnik PIT: 1193182 / 60 ≈ 19886 */
#define PIT_DIVISOR     (1193182U / TIMER_HZ)
/* Oryginalny dzielnik PIT (18.2 Hz) */
#define PIT_DIVISOR_ORIG 65536U

/* Liczba tików co sekundę — alias */
#define TICKS_PER_SEC   TIMER_HZ

/* Globalny licznik klatek — volatile, inkrementowany z ISR */
extern volatile uint32_t tick_count;

/* Instaluje ISR na INT 8, przeprogramowuje PIT na TIMER_HZ.
   MUSI być wywołane przed główną pętlą gry. */
void timer_init(void);

/* Przywraca oryginalny handler INT 8 i dzielnik PIT.
   Wywołaj przed wyjściem z programu. */
void timer_shutdown(void);

/* Blokuje do następnego tiku timera (synchronizacja 60 fps).
   Odpowiednik NES waitframe (nes/Sys/general.asm). */
void waitframe(void);

/* Zwraca aktualny tick_count bez blokowania. */
static inline uint32_t timer_get(void) { return tick_count; }

/* Zwraca czas w ms od uruchomienia timera. */
uint32_t timer_ms(void);

#endif /* TIMER_H */
