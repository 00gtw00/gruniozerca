/*
 * player.h — Gracz (Grunio) — port z nes/Sys/Obj/gracz.asm
 * Gruniożerca DOS port
 *
 * Zachowane oryginalne stałe z nes/Macro.asm:31-42.
 * Fixed-point 8.8: pozycja = int16_t (górny bajt = piksele, dolny = ułamek).
 */
#ifndef PLAYER_INCLUDE_H
#define PLAYER_INCLUDE_H

#include <stdint.h>
#include "dos_compat.h"

/* ---------- Stałe z nes/Macro.asm:31-42 ----------------------------------- */
#define GRUNIO_ACC        0x10   /* przyspieszenie poziome (oryg. 0x04 → za ślisko) */
#define GRUNIO_START_SPD  0x0100 /* prędkość startowa (fp8: 1.0 px/klatkę) */
#define GRUNIO_TOP_SPD    0x0250 /* prędkość maksymalna (fp8: ~2.3 px/klatkę) */
#define PLAYER_W          20     /* szerokość kolizji [piksele] */
#define PLAYER_H          10     /* wysokość kolizji [piksele] */

/* ---------- Animacja radości po złapaniu marchewki ----------------------- */
#define JOY_FRAMES     20       /* klatki trwania radości */
#define JOY_JUMP_VY  (-512)     /* fp8 prędkość startowa w górę (~2 px/klatkę) */
#define JOY_GRAVITY    0x40     /* fp8 przyspieszenie grawitacji (~0.25 px/klatkę²) */

/* ---------- Kolory gracza (0..3, cykl przez A/akcję) ---------------------- */
#define PLAYER_COLOR_COUNT 4

/* ---------- Stany animacji ------------------------------------------------- */
typedef enum {
    ANIM_IDLE  = 0,
    ANIM_WALK1 = 1,
    ANIM_WALK2 = 2
} PlayerAnim;

/* ---------- Struktura gracza ----------------------------------------------- */
typedef struct {
    /* pozycja — fp8: [15:8] = piksele, [7:0] = ułamek */
    fp8_t  x;          /* pozycja X (fp8) */
    fp8_t  x_frac;     /* akumulator ułamkowy X — odpowiednik Ob.c (xposl) */
    int16_t y;         /* pozycja Y (piksele, stała w tej grze) */

    /* prędkość */
    fp8_t  x_speed;    /* aktualna prędkość X (fp8) — Ob.a/Ob.b (xspeedl/h) */

    /* animacja */
    PlayerAnim anim;
    uint8_t    anim_frame;  /* klatka animacji */
    uint8_t    anim_timer;  /* licznik opóźnienia klatki */
    uint8_t    facing_left; /* 1 = patrzy w lewo (lustro sprite) */

    /* radość — podskoczenie i wiggle po złapaniu marchewki */
    fp8_t   joy_vy;     /* prędkość pionowa skoku radości (fp8) */
    fp8_t   y_offset;   /* aktualny offset Y (fp8, ≤0 = w górę) */
    uint8_t joy_timer;  /* klatki pozostałe animacji radości */
    uint8_t joy_flip;   /* flaga wizualnej zmiany kierunku (0/1) */

    /* stan gry */
    uint8_t    color;       /* aktualny kolor 0..3 (pigcolor z NES) */
    uint8_t    alive;       /* 1 = żyje */
    uint8_t    lives;       /* liczba żyć */
} Player;

/* ---------- API ------------------------------------------------------------ */

/* Inicjalizacja gracza na pozycji startowej. */
void player_init(Player *p);

/* Aktualizacja logiki gracza (raz na klatkę).
   Port z nes/Sys/Obj/gracz.asm — ruch, przyspieszenie, granice ekranu. */
void player_update(Player *p);

/* Rysuje gracza na back buffer (meta-sprite złożony z kafelków 8×8). */
void player_draw(const Player *p);

/* Wyzwala animację radości (podskoczenie + wiggle kierunku).
   Wywołać gdy marchewka właściwego koloru zostanie złapana. */
void player_notify_catch(Player *p);

/* Zwraca X gracza w pikselach (integer part of fp8). */
static inline int player_x(const Player *p) { return FP8_INT(p->x); }

#endif /* PLAYER_INCLUDE_H */
