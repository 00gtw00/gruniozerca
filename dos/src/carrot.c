/*
 * carrot.c — Marchewki (gold objects) — port z nes/Sys/Obj/gold.asm
 * Gruniożerca DOS port, 2024
 *
 * Pool MAXOBJ=16 marchewek, grawitacja fp8, 4 kolory, combo scoring.
 * Stałe prędkości z nes/Macro.asm:37-42.
 */
#include "carrot.h"
#include "player.h"
#include "video.h"
#include "sound.h"
#include <string.h>
#include <stdlib.h>

/* ---------- Meta-sprite marchewki ----------------------------------------- */
/* Tile IDs z NES nes/Sys/Sprity.asm sgold:
 *   db 0,0,$54,2   → tile 0x54=84 lewy góra
 *   db 8,0,$55,2   → tile 0x55=85 prawy góra
 *   db 2,8,$52,2   → tile 0x52=82 środek (szer. 1 tile, wcięcie 2px)
 *   db 2,16,$53,2  → tile 0x53=83 dół
 * Format DOS: { tile_id, dx, dy, palette, flip_h, ..., 0xFF }
 * palette podmieniana dynamicznie na kolor marchewki w carrot_pool_draw() */
static const uint8_t meta_carrot[] = {
    0x54,  0,  0, 0, 0,   /* tile 84 — lewy góra */
    0x55,  8,  0, 0, 0,   /* tile 85 — prawy góra */
    0x52,  2,  8, 0, 0,   /* tile 82 — środek */
    0x53,  2, 16, 0, 0,   /* tile 83 — dół */
    0xFF
};

/* ---------- Spawn timer — trudność rośnie liniowo z czasem --------------- */
/* Na początku: rzadko (min=60, max=120); po ~60s: często (min=15, max=40) */
#define SPAWN_MIN_START   60
#define SPAWN_MAX_START  120
#define SPAWN_MIN_END     15
#define SPAWN_MAX_END     40
#define DIFFICULTY_RAMP  3600u   /* klatki do osiągnięcia maks. trudności */

static uint8_t  spawn_timer        = 0;
static uint32_t s_difficulty_timer = 0;   /* klatki od startu rundy */

static int spawn_min_cur(void) {
    if (s_difficulty_timer >= DIFFICULTY_RAMP) return SPAWN_MIN_END;
    return SPAWN_MIN_START + (int)s_difficulty_timer *
           (SPAWN_MIN_END - SPAWN_MIN_START) / (int)DIFFICULTY_RAMP;
}
static int spawn_max_cur(void) {
    if (s_difficulty_timer >= DIFFICULTY_RAMP) return SPAWN_MAX_END;
    return SPAWN_MAX_START + (int)s_difficulty_timer *
           (SPAWN_MAX_END - SPAWN_MAX_START) / (int)DIFFICULTY_RAMP;
}
static int     s_miss_flag  = 0;   /* marchewka upadła na ziemię */
static int     s_catch_flag = 0;   /* złapano marchewkę właściwego koloru */

int carrot_had_catch(void) {
    int v = s_catch_flag;
    s_catch_flag = 0;
    return v;
}

int carrot_had_miss(void) {
    int v = s_miss_flag;
    s_miss_flag = 0;
    return v;
}

/* ---------- Inicjalizacja ------------------------------------------------- */
void carrot_pool_init(CarrotPool *pool) {
    memset(pool, 0, sizeof(*pool));
    spawn_timer        = SPAWN_MIN_START;
    s_difficulty_timer = 0;
}

/* ---------- Spawn --------------------------------------------------------- */
void carrot_spawn(CarrotPool *pool) {
    /* Znajdź wolny slot */
    for (int i = 0; i < MAXOBJ; i++) {
        if (!pool->items[i].active) {
            Carrot *c = &pool->items[i];
            c->active      = 1;
            /* Losowa pozycja X w obszarze gry */
            c->x           = PLAY_X_OFFSET + (rand() % (PLAY_W - 8));
            c->y           = FP8(-8);       /* poza górną krawędzią */
            c->y_frac      = 0;
            c->speed       = (fp8_t)GOLD_START_SPD;
            c->color       = pool->next_color;
            pool->next_color = (pool->next_color + 1) % CARROT_COLOR_COUNT;
            c->wait        = 0;
            c->anim_frame  = 0;
            c->anim_timer  = 0;
            pool->count++;
            return;
        }
    }
}

/* ---------- Kolizja AABB -------------------------------------------------- */
int carrot_collide_player(const Carrot *c, int px, int py) {
    int cx = (int)c->x;
    int cy = FP8_INT(c->y);
    /* AABB: marchewka 8×8, gracz PLAYER_W×PLAYER_H */
    return (cx + 8  > px &&
            cx      < px + PLAYER_W &&
            cy + 8  > py &&
            cy      < py + PLAYER_H);
}

/* ---------- Aktualizacja -------------------------------------------------- */
void carrot_pool_update(CarrotPool *pool, int player_x, int player_y,
                        int player_color, uint32_t *score) {
    /* Trudność i spawn timer */
    s_difficulty_timer++;
    if (spawn_timer > 0) {
        spawn_timer--;
    } else {
        carrot_spawn(pool);
        int smin = spawn_min_cur();
        int smax = spawn_max_cur();
        spawn_timer = (uint8_t)(smin + (rand() % (smax - smin)));
    }

    for (int i = 0; i < MAXOBJ; i++) {
        Carrot *c = &pool->items[i];
        if (!c->active) continue;

        /* ---- Grawitacja (fp8): speed += GOLD_ACC, clamp do TOP_SPD ---- */
        c->speed += GOLD_ACC;
        if (c->speed > GOLD_TOP_SPD)
            c->speed = GOLD_TOP_SPD;

        /* ---- Ruch w dół (fp8) ---- */
        /* Odpowiednik NES add16 goldyl, goldspdl */
        int32_t new_y = (int32_t)c->y + (int32_t)c->speed;
        c->y = (fp8_t)new_y;

        int cy = FP8_INT(c->y);

        /* ---- Marchewka poza dolną krawędzią → nieudana ---- */
        if (cy >= (int)UI_Y) {
            c->active = 0;
            pool->count--;
            carrot_reset_combo(pool);
            sound_play_sfx(SFX_MISS);
            s_miss_flag = 1;
            continue;
        }

        /* ---- Kolizja z graczem ---- */
        if (carrot_collide_player(c, player_x, player_y)) {
            c->active = 0;
            pool->count--;

            if (c->color == (uint8_t)player_color) {
                /* Trafienie! Zlicz combo i dodaj punkty */
                uint8_t combo_idx = pool->combo;
                if (combo_idx > 7) combo_idx = 7;
                uint16_t pts = CARROT_COMBO_PTS[combo_idx];
                *score += pts;
                pool->combo++;
                s_catch_flag = 1;
                sound_play_sfx(SFX_CATCH);
            } else {
                /* Zły kolor — reset combo, kara jak za pudło */
                pool->combo = 0;
                s_miss_flag = 1;
                sound_play_sfx(SFX_MISS);
            }
        }

        /* ---- Animacja ---- */
        c->anim_timer++;
        if (c->anim_timer >= 8) {
            c->anim_timer = 0;
            c->anim_frame = (c->anim_frame + 1) % 4;
        }
    }
}

void carrot_reset_combo(CarrotPool *pool) {
    pool->combo = 0;
}

/* ---------- Rysowanie ----------------------------------------------------- */
void carrot_pool_draw(const CarrotPool *pool) {
    for (int i = 0; i < MAXOBJ; i++) {
        const Carrot *c = &pool->items[i];
        if (!c->active) continue;

        int cx = (int)c->x;
        int cy = FP8_INT(c->y);
        if (cy < -24 || cy >= (int)SCREEN_H) continue;

        /* Skopiuj wszystkie 4 kafelki meta-sprite'a marchewki,
         * podmieniając pole palety na kolor marchewki */
        uint8_t ms[32];   /* 4 entries × 5 bajtów + terminator */
        const uint8_t *src = meta_carrot;
        uint8_t       *dst = ms;
        while (*src != 0xFF) {
            dst[0] = src[0];   /* tile_id */
            dst[1] = src[1];   /* dx */
            dst[2] = src[2];   /* dy */
            dst[3] = c->color; /* palette → kolor marchewki */
            dst[4] = src[4];   /* flip_h */
            src += 5; dst += 5;
        }
        *dst = 0xFF;
        video_draw_meta_sprite(cx, cy, ms);
    }
}
