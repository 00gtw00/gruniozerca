/*
 * player.c — Logika gracza (Grunio) — port z nes/Sys/Obj/gracz.asm
 * Gruniożerca DOS port, 2024
 *
 * Zachowane oryginalne stałe prędkości i przyspieszenia.
 * Fixed-point 8.8: prędkość i pozycja.
 * Granice: PLAY_X_OFFSET .. PLAY_X_OFFSET + PLAY_W - PLAYER_W
 */
#include "player.h"
#include "input.h"
#include "video.h"
#include <string.h>

/* ---------- Sprite'y meta-sprite Gruni -------------------------------------- */
/* Format: { tile_id, dx, dy, palette, flip_h, ..., 0xFF }
 *
 * Tile IDs z nes/Sys/Sprity.asm — obie strony CHR używają tabeli 0 ($0000).
 *
 * sgrunioidle: ciało 3×2 kafelki (24×16px) + 2 kafelki ogona
 *   Ciało: tile 0x30-0x35 (48-53)
 *   Ogon:  tile 0x2E-0x2F (46-47), paleta 1
 *
 * sgruniowalk1: tile 0x36-0x3B (54-59)
 * sgruniowalk2: tile 0x3C-0x41 (60-65)
 *
 * Sprite skierowany w prawo — 3 kolumny (dx=0,8,16), 2 wiersze (dy=0,8).
 * Lustro lewe — kolejność kolumn odwrócona, flip_h=1.
 */

/* ---- Idle (prawy) ---- */
static const uint8_t meta_idle[] = {
    0x30,  0, 0, 0, 0,
    0x31,  8, 0, 0, 0,
    0x32, 16, 0, 0, 0,
    0x33,  0, 8, 0, 0,
    0x34,  8, 8, 0, 0,
    0x35, 16, 8, 0, 0,
    0x2E, 12,(uint8_t)-1, 1, 0,   /* ogon góra */
    0x2F, 12, 7, 1, 0,            /* ogon dół */
    0xFF
};

/* ---- Idle (lewy) — kolumny odwrócone, flip_h=1 ---- */
static const uint8_t meta_idle_left[] = {
    0x32,  0, 0, 0, 1,
    0x31,  8, 0, 0, 1,
    0x30, 16, 0, 0, 1,
    0x35,  0, 8, 0, 1,
    0x34,  8, 8, 0, 1,
    0x33, 16, 8, 0, 1,
    0x2F,  4,(uint8_t)-1, 1, 1,   /* ogon góra (mirror) */
    0x2E,  4, 7, 1, 1,            /* ogon dół (mirror) */
    0xFF
};

/* ---- Walk1 (prawy) ---- */
static const uint8_t meta_walk1[] = {
    0x36,  0,(uint8_t)-1, 0, 0,
    0x37,  8,(uint8_t)-1, 0, 0,
    0x38, 16,(uint8_t)-1, 0, 0,
    0x39,  0, 7, 0, 0,
    0x3A,  8, 7, 0, 0,
    0x3B, 16, 7, 0, 0,
    0x2E, 12,(uint8_t)-2, 1, 0,
    0x2F, 12, 6, 1, 0,
    0xFF
};

/* ---- Walk1 (lewy) ---- */
static const uint8_t meta_walk1_left[] = {
    0x38,  0,(uint8_t)-1, 0, 1,
    0x37,  8,(uint8_t)-1, 0, 1,
    0x36, 16,(uint8_t)-1, 0, 1,
    0x3B,  0, 7, 0, 1,
    0x3A,  8, 7, 0, 1,
    0x39, 16, 7, 0, 1,
    0x2F,  4,(uint8_t)-2, 1, 1,
    0x2E,  4, 6, 1, 1,
    0xFF
};

/* ---- Walk2 (prawy) ---- */
static const uint8_t meta_walk2[] = {
    0x3C,  0,(uint8_t)-1, 0, 0,
    0x3D,  8,(uint8_t)-1, 0, 0,
    0x3E, 16,(uint8_t)-1, 0, 0,
    0x3F,  0, 7, 0, 0,
    0x40,  8, 7, 0, 0,
    0x41, 16, 7, 0, 0,
    0x2E, 12,(uint8_t)-2, 1, 0,
    0x2F, 12, 6, 1, 0,
    0xFF
};

/* ---- Walk2 (lewy) ---- */
static const uint8_t meta_walk2_left[] = {
    0x3E,  0,(uint8_t)-1, 0, 1,
    0x3D,  8,(uint8_t)-1, 0, 1,
    0x3C, 16,(uint8_t)-1, 0, 1,
    0x41,  0, 7, 0, 1,
    0x40,  8, 7, 0, 1,
    0x3F, 16, 7, 0, 1,
    0x2F,  4,(uint8_t)-2, 1, 1,
    0x2E,  4, 6, 1, 1,
    0xFF
};

/* Tablica meta-sprite'ów [anim][facing_left] */
static const uint8_t * const player_sprites[3][2] = {
    { meta_idle,  meta_idle_left  },
    { meta_walk1, meta_walk1_left },
    { meta_walk2, meta_walk2_left },
};

/* ---------- Tempo animacji (klatki na animację) ----------------------------- */
#define WALK_ANIM_SPEED 8  /* zmiana klatki co 8 ticków */

/* ---------- Granice poziome obszaru gry ------------------------------------ */
#define PLAY_X_MIN  (PLAY_X_OFFSET)
#define PLAY_X_MAX  (PLAY_X_OFFSET + PLAY_W - PLAYER_W)
/* Y gracza stała — dno obszaru gry.
 * 16 = wizualna wysokość meta-sprite (2 rzędy × 8px), niezależna od PLAYER_H. */
#define PLAYER_VISUAL_H  16
#define PLAYER_Y         (UI_Y - PLAYER_VISUAL_H)

/* ---------- Implementacja -------------------------------------------------- */

void player_init(Player *p) {
    memset(p, 0, sizeof(*p));
    p->x       = FP8(PLAY_X_OFFSET + PLAY_W / 2 - PLAYER_W / 2);
    p->y       = PLAYER_Y;
    p->x_speed = FP8(0);
    p->x_frac  = 0;
    p->anim    = ANIM_IDLE;
    p->anim_frame = 0;
    p->anim_timer = 0;
    p->facing_left = 0;
    p->color   = 0;
    p->alive   = 1;
    p->lives   = 3;
}

/* Port z nes/Sys/Obj/gracz.asm — logika ruchu z przyspieszeniem fp8.
 *
 * Naprawiona fizyka w stosunku do oryginału NES:
 *  - GRUNIO_START_SPD: natychmiastowy skok prędkości przy starcie / zmianie kierunku
 *    (zapobiega efektowi "lodu" gdy ACC=0x04 rozgrzewało się przez 148 klatek)
 *  - Hamowanie 3× ACC zamiast 2× — ostrzejsze zatrzymanie
 */
void player_update(Player *p) {
    if (!p->alive) return;

    int moving = 0;

    /* ---- Animacja radości (niezależna od ruchu) ---- */
    if (p->joy_timer > 0) {
        /* Fizyka skoku: najpierw ruch, potem grawitacja */
        p->y_offset += p->joy_vy;
        p->joy_vy   += JOY_GRAVITY;
        if (p->y_offset >= 0) {
            p->y_offset = 0;
            p->joy_vy   = 0;
        }
        /* Dwie szybkie zmiany wizualnego kierunku — efekt wiggle */
        if (p->joy_timer == JOY_FRAMES - 3) p->joy_flip ^= 1;  /* zaraz po starcie */
        if (p->joy_timer == JOY_FRAMES - 9) p->joy_flip ^= 1;  /* przy szczycie */
        p->joy_timer--;
        if (p->joy_timer == 0) p->joy_flip = 0;
    }

    /* ---- Ruch w prawo ---- */
    if (input_held(ACT_RIGHT)) {
        p->facing_left = 0;
        moving = 1;
        if (p->x_speed <= 0) {
            /* Start lub zmiana kierunku: natychmiastowy skok do START_SPD */
            p->x_speed = (fp8_t)GRUNIO_START_SPD;
        } else {
            p->x_speed += GRUNIO_ACC;
            if (p->x_speed > (fp8_t)GRUNIO_TOP_SPD)
                p->x_speed = (fp8_t)GRUNIO_TOP_SPD;
        }
    }
    /* ---- Ruch w lewo ---- */
    else if (input_held(ACT_LEFT)) {
        p->facing_left = 1;
        moving = 1;
        if (p->x_speed >= 0) {
            /* Start lub zmiana kierunku: natychmiastowy skok do START_SPD */
            p->x_speed = -(fp8_t)GRUNIO_START_SPD;
        } else {
            p->x_speed -= GRUNIO_ACC;
            if (p->x_speed < -(fp8_t)GRUNIO_TOP_SPD)
                p->x_speed = -(fp8_t)GRUNIO_TOP_SPD;
        }
    }
    /* ---- Hamowanie (3× ACC — ostrzejsze niż oryginalne 2×) ---- */
    else {
        if (p->x_speed > 0) {
            p->x_speed -= GRUNIO_ACC * 3;
            if (p->x_speed < 0) p->x_speed = 0;
        } else if (p->x_speed < 0) {
            p->x_speed += GRUNIO_ACC * 3;
            if (p->x_speed > 0) p->x_speed = 0;
        }
    }

    /* ---- Aktualizacja pozycji (fp8 + fp8 → przeniesienie do integer) ---- */
    /* Odpowiednik NES: add16 xposl, xspeedl (Ob.c += Ob.a, Ob.x += Ob.b + carry) */
    int32_t new_pos = (int32_t)p->x + (int32_t)p->x_speed;

    /* ---- Granice ekranu ---- */
    int32_t x_min_fp = FP8(PLAY_X_MIN);
    int32_t x_max_fp = FP8(PLAY_X_MAX);
    if (new_pos < x_min_fp) {
        new_pos = x_min_fp;
        p->x_speed = 0;
    }
    if (new_pos > x_max_fp) {
        new_pos = x_max_fp;
        p->x_speed = 0;
    }
    p->x = (fp8_t)new_pos;

    /* ---- Zmiana koloru (ACT_ACTION) ---- */
    if (input_pressed(ACT_ACTION)) {
        p->color = (p->color + 1) % PLAYER_COLOR_COUNT;
    }

    /* ---- Animacja ---- */
    if (!moving || p->x_speed == 0) {
        p->anim = ANIM_IDLE;
        p->anim_timer = 0;
    } else {
        p->anim_timer++;
        if (p->anim_timer >= WALK_ANIM_SPEED) {
            p->anim_timer = 0;
            /* Przełączaj między WALK1 i WALK2 */
            p->anim = (p->anim == ANIM_WALK1) ? ANIM_WALK2 : ANIM_WALK1;
        }
    }
}

void player_draw(const Player *p) {
    if (!p->alive) return;

    int  x   = FP8_INT(p->x);
    int  y   = p->y + FP8_INT(p->y_offset);   /* skok radości: offset pionowy */
    int  anim_idx = (int)p->anim;
    int  fl   = (int)p->facing_left ^ (int)p->joy_flip;  /* wiggle wizualny */

    /* Wybierz meta-sprite na podstawie animacji i kierunku */
    const uint8_t *sprite = player_sprites[anim_idx][fl];

    /* Kolor gracza offsetuje paletę sprite'a (0..3) */
    /* Rysujemy meta-sprite z modyfikacją palety przez kolor */
    /* Kopiujemy meta-sprite i podmieniamy pole palety */
    uint8_t ms[64];
    const uint8_t *src = sprite;
    uint8_t       *dst = ms;
    while (*src != 0xFF) {
        dst[0] = src[0];           /* tile_id */
        dst[1] = src[1];           /* dx */
        dst[2] = src[2];           /* dy */
        dst[3] = p->color;         /* palette → kolor gracza */
        dst[4] = src[4];           /* flip_h */
        src += 5; dst += 5;
    }
    *dst = 0xFF;

    video_draw_meta_sprite(x, y, ms);
}

/* Wyzwala animację radości — podskoczenie + dwa szybkie obroty */
void player_notify_catch(Player *p) {
    if (!p->alive) return;
    p->joy_timer = JOY_FRAMES;
    p->joy_vy    = (fp8_t)JOY_JUMP_VY;
    p->y_offset  = 0;
    p->joy_flip  = 0;
}
