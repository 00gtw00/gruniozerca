/*
 * hiscore.c — Ekran nowego rekordu
 * Gruniożerca DOS port, 2024
 *
 * Tło: hiscore.pcx, pełna paleta (256 slotów).
 * Świnka: po aplikacji palety PCX przywracamy sloty 0-7 z game_pal (NES)
 *   → sub-paleta 0 (ciało, sloty 1-3) i sub-paleta 1 (ogon, sloty 5-7).
 * Wynik: video_draw_string z bezpośrednim kolorem VGA (bez offsetu palety NES).
 */
#include "hiscore.h"
#include "video.h"
#include "input.h"
#include "timer.h"
#include "score.h"
#include "sound.h"
#include "pcx.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifndef HISCORE_H
#define HISCORE_H
void hiscore_run(void);
#endif

/* Kolory bezpośrednie z palety hiscore.pcx */
#define HI_TITLE_COLOR   2    /* prawie czarny */
#define HI_LABEL_COLOR   5    /* ciemny brąz */
#define HI_BLINK_COLOR  72    /* pomarańczowy */

static uint8_t s_bg[SCREEN_W * SCREEN_H];
static uint8_t s_pal[768];
static int     s_loaded = 0;

static void ensure_loaded(void) {
    if (s_loaded) return;
    if (pcx_load_buf("assets/hiscore.pcx", s_bg, sizeof(s_bg), s_pal) == PCX_OK)
        s_loaded = 1;
}

static void apply_palette(void) {
    int i;
    if (!s_loaded) return;
    /* Pełna paleta hiscore.pcx — tło poprawne */
    for (i = 0; i < 256; i++)
        video_set_color((uint8_t)i,
                        s_pal[i*3+0] >> 2,
                        s_pal[i*3+1] >> 2,
                        s_pal[i*3+2] >> 2);
    /* Przywróć NES sub-palety 0-1 (sloty 0-7) dla świnki:
     *   0-3: ciało gracza (palette=0 → pixel + 0*4 = VGA 1-3)
     *   4-7: ogon (palette=1 → pixel + 1*4 = VGA 5-7) */
    video_restore_game_palette_range(0, 8);
}

static void draw_score_str(int x, int y, uint32_t val, uint8_t color) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%06u", (unsigned)(val % 1000000u));
    video_draw_string(x, y, buf, color);
}

/* ---- Meta-sprite'y chodu Gruni (walk1/walk2 prawy, z player.c) ----------- */
static const uint8_t hi_walk1[] = {
    0x36,  0, 255, 0, 0,
    0x37,  8, 255, 0, 0,
    0x38, 16, 255, 0, 0,
    0x39,  0,   7, 0, 0,
    0x3A,  8,   7, 0, 0,
    0x3B, 16,   7, 0, 0,
    0xFF
};
static const uint8_t hi_walk2[] = {
    0x3C,  0, 255, 0, 0,
    0x3D,  8, 255, 0, 0,
    0x3E, 16, 255, 0, 0,
    0x3F,  0,   7, 0, 0,
    0x40,  8,   7, 0, 0,
    0x41, 16,   7, 0, 0,
    0xFF
};

void hiscore_run(void) {
    ensure_loaded();
    apply_palette();
    sound_play_music(MUS_HISCORE);

    int grunio_x      = -24;
    int grunio_target = (SCREEN_W - 24) / 2;
    int anim_frame    = 0;
    int anim_timer    = 0;
    uint32_t show_timer = 0;

    const uint8_t * const walk_frames[2] = { hi_walk1, hi_walk2 };

    video_clear();
    while (video_fade_step(1)) {
        waitframe();
        video_flip();
    }

    while (1) {
        waitframe();
        input_update();
        sound_update();
        show_timer++;

        if (s_loaded)
            memcpy(video_backbuf, s_bg, sizeof(s_bg));
        else
            video_clear();

        /* "NOWY REKORD!" */
        {
            const char *rec = "NOWY REKORD!";
            int rx = (SCREEN_W - (int)(strlen(rec) * TILE_W)) / 2;
            video_draw_string(rx, 50, rec, HI_TITLE_COLOR);
        }

        /* Wynik */
        {
            int sx = (SCREEN_W - 6 * TILE_W) / 2;
            draw_score_str(sx, 66, score_get_hi(), HI_TITLE_COLOR);
        }

        /* Animacja Gruni */
        if (grunio_x < grunio_target)
            grunio_x += 2;
        anim_timer++;
        if (anim_timer >= 8) {
            anim_timer = 0;
            anim_frame = 1 - anim_frame;
        }
        video_draw_meta_sprite(grunio_x, 105, walk_frames[anim_frame]);

        if (show_timer > 120) {
            const char *press = "WCISNIJ ENTER";
            int px = (SCREEN_W - (int)(strlen(press) * TILE_W)) / 2;
            if ((show_timer / 30) % 2 == 0)
                video_draw_string(px, 145, press, HI_BLINK_COLOR);
            if (input_pressed(ACT_START) || input_pressed(ACT_ACTION))
                break;
        }

        video_flip();
    }

    sound_stop_music();
    while (video_fade_step(-1)) {
        waitframe();
        video_flip();
    }
}
