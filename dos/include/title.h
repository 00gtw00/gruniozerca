/*
 * title.h — Ekran tytułowy
 * Gruniożerca DOS port
 */
#ifndef TITLE_H
#define TITLE_H

typedef enum {
    TITLE_RESULT_START = 0,
    TITLE_RESULT_QUIT  = 1
} TitleResult;

/* Uruchom ekran tytułowy; zwraca wynik akcji gracza. */
TitleResult title_run(void);

/* Ekran "Jak grać?" — pokazuj tylko gdy high score == 0 (pierwsze uruchomienie). */
void tutorial_run(void);

#endif /* TITLE_H */
