/*
 * score.h — Wynik i high score — port z nes/Sys/hscore.asm
 * Gruniożerca DOS port
 *
 * Wynik: 7 cyfr dziesiętnych (uint32_t wystarczy dla max 9 999 999).
 * High score: zapis/odczyt z GRUNIO.SAV.
 */
#ifndef SCORE_H
#define SCORE_H

#include <stdint.h>

#define SCORE_MAX_DIGITS 6
#define HISCORE_FILE     "GRUNIO.SAV"

/* ---------- API ------------------------------------------------------------ */

/* Inicjalizacja: zeruje wynik, ładuje high score z pliku (jeśli istnieje). */
void score_init(void);

/* Dodaje punkty do aktualnego wyniku. */
void score_add(uint32_t points);

/* Zwraca aktualny wynik. */
uint32_t score_get(void);

/* Zwraca aktualny high score. */
uint32_t score_get_hi(void);

/* Sprawdza czy aktualny wynik jest nowym rekordem.
   Jeśli tak, automatycznie aktualizuje high score. Zwraca 1 jeśli nowy rekord. */
int score_check_hi(void);

/* Zapisuje high score do GRUNIO.SAV. */
void score_save_hi(void);

/* Zeruje wynik bieżący (nowa gra). */
void score_reset(void);

/* Rysuje wynik na back buffer (x,y — lewy górny róg, 7 cyfr 8×8 px).
   Używa kafelków cyfr z sprites.dat. */
void score_draw(int x, int y, uint32_t val);

/* Rysuje etykietę "SCORE" i aktualny wynik na pasku UI. */
void score_draw_ui(void);

/* Rysuje etykietę "HI" i high score na pasku UI. */
void score_draw_hi_ui(void);

#endif /* SCORE_H */
