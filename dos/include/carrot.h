/*
 * carrot.h — Marchewki (gold) — port z nes/Sys/Obj/gold.asm
 * Gruniożerca DOS port
 *
 * Pool 16 marchewek (maxobj=16), grawitacja fp8, 4 kolory, combo scoring.
 * Stałe z nes/Macro.asm:37-42.
 */
#ifndef CARROT_H
#define CARROT_H

#include <stdint.h>
#include "dos_compat.h"

/* ---------- Stałe z nes/Macro.asm ----------------------------------------- */
#define MAXOBJ           16    /* maksymalna liczba obiektów (marchewek) */
#define GOLD_ACC         0x06  /* przyspieszenie grawitacji (NES: 0x0E) */

/* ===== PRĘDKOŚĆ MARCHEWEK — zmień tę wartość, by dostosować tempo =====
 * 100 = oryginalna prędkość NES, zmniejsz by spowolnić (np. 60–80)     */
#define CARROT_SPEED_PCT  70

#define GOLD_START_SPD   ((fp8_t)(0x0080 * CARROT_SPEED_PCT / 100))
#define GOLD_TOP_SPD     ((fp8_t)(0x0150 * CARROT_SPEED_PCT / 100))

/* ---------- Kolory marchewek ----------------------------------------------- */
#define CARROT_COLOR_COUNT 4

/* Punkty za złapanie wg combo — BCD z NES (0x11..0x35) minus 1 każda wartość */
static const uint16_t CARROT_COMBO_PTS[8] = {
    10, 14, 20, 21, 24, 30, 31, 34
};

/* ---------- Struktura marchewki ------------------------------------------- */
typedef struct {
    uint8_t  active;    /* 1 = slot zajęty */

    /* pozycja */
    int16_t  x;         /* pozycja X [piksele] */
    fp8_t    y;         /* pozycja Y (fp8) — odpowiednik Ob.y + Ob.c (goldyl) */
    fp8_t    y_frac;    /* akumulator ułamkowy Y */

    /* prędkość (fp8) — Ob.a/Ob.b (goldspdl/h) */
    fp8_t    speed;

    /* stan */
    uint8_t  color;     /* kolor 0..3 (goldcol) */
    uint8_t  wait;      /* odliczanie przed pojawieniem się (goldwait) */

    /* animacja */
    uint8_t  anim_frame;
    uint8_t  anim_timer;
} Carrot;

/* Pool marchewek */
typedef struct {
    Carrot   items[MAXOBJ];
    uint8_t  count;      /* aktywnych */
    uint8_t  combo;      /* aktualny combo (nes: combo @$009E) */
    uint8_t  next_color; /* następny kolor przy spawnie */
} CarrotPool;

/* ---------- API ------------------------------------------------------------ */

/* Inicjalizacja puli — zerowanie wszystkich slotów. */
void carrot_pool_init(CarrotPool *pool);

/* Aktualizacja wszystkich marchewek (raz na klatkę).
   Zastosuje grawitację, sprawdzi kolizję z graczem, doda punkty.
   player_x, player_y — środek gracza; player_color — aktualny kolor. */
void carrot_pool_update(CarrotPool *pool, int player_x, int player_y,
                        int player_color, uint32_t *score);

/* Rysuje wszystkie aktywne marchewki na back buffer. */
void carrot_pool_draw(const CarrotPool *pool);

/* Próba spawnu nowej marchewki (jeśli jest wolny slot). */
void carrot_spawn(CarrotPool *pool);

/* Resetuje combo po spudłowaniu (marchewka dotarła do dołu). */
void carrot_reset_combo(CarrotPool *pool);

/* Zwraca 1 jeśli w ostatnim update marchewka właściwego koloru została złapana.
 * Resetuje flagę przy każdym wywołaniu. */
int carrot_had_catch(void);

/* Zwraca 1 jeśli w ostatnim update choć jedna marchewka upadła na ziemię.
 * Resetuje flagę przy każdym wywołaniu. */
int carrot_had_miss(void);

/* Kolizja AABB marchewki z graczem.
   Zwraca 1 jeśli zachodzi kolizja. */
int carrot_collide_player(const Carrot *c, int px, int py);

#endif /* CARROT_H */
