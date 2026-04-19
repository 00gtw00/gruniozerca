/*
 * gameover.c — Ekran KONIEC GRY
 * Gruniożerca DOS port, 2024
 *
 * Tło: settings.pcx, pełna paleta (jak title.c). Brak sprite'ów NES na tym
 * ekranie — nie ma konfliktu palet. Wynik renderowany przez video_draw_string
 * (bezpośredni kolor VGA) zamiast score_draw (video_draw_sprite + offset palety).
 */
#include "gameover.h"
#include "video.h"
#include "input.h"
#include "timer.h"
#include "score.h"
#include "sound.h"
#include "pcx.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifndef GAMEOVER_H
#define GAMEOVER_H
void gameover_run(void);
#endif

/* Kolory z palety settings.pcx (indeksy bezpośrednie, identyczne jak w title.c) */
#define GO_TITLE_COLOR   2    /* prawie czarny */
#define GO_LABEL_COLOR   5    /* ciemny brąz */
#define GO_BLINK_COLOR  72    /* pomarańczowy */

static uint8_t s_bg[SCREEN_W * SCREEN_H];
static uint8_t s_pal[768];
static int     s_loaded = 0;

static void ensure_loaded(void) {
    if (s_loaded) return;
    if (pcx_load_buf("assets/settings.pcx", s_bg, sizeof(s_bg), s_pal) == PCX_OK)
        s_loaded = 1;
}

static void apply_palette(void) {
    int i;
    if (!s_loaded) return;
    /* Pełna paleta settings.pcx — jak w title.c set_palette_apply() */
    for (i = 0; i < 256; i++)
        video_set_color((uint8_t)i,
                        s_pal[i*3+0] >> 2,
                        s_pal[i*3+1] >> 2,
                        s_pal[i*3+2] >> 2);
}

/* Rysuje 6-cyfrowy wynik jako tekst z bezpośrednim kolorem VGA.
 * video_draw_string używa fg_color wprost, bez offsetu palety NES. */
static void draw_score_str(int x, int y, uint32_t val, uint8_t color) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%06u", (unsigned)(val % 1000000u));
    video_draw_string(x, y, buf, color);
}

void gameover_run(void) {
    ensure_loaded();
    apply_palette();
    sound_play_music(MUS_GAMEOVER);

    uint32_t show_timer = 0;

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

        /* "KONIEC GRY" — wyśrodkowane */
        {
            const char *go = "KONIEC GRY";
            int gx = (SCREEN_W - (int)(strlen(go) * TILE_W)) / 2;
            video_draw_string(gx, 70, go, GO_TITLE_COLOR);
        }

        /* Etykieta + wynik — wyśrodkowane */
        {
            const char *lbl = "WYNIK:";
            int lx = (SCREEN_W - (int)(strlen(lbl) * TILE_W)) / 2;
            video_draw_string(lx, 96, lbl, GO_LABEL_COLOR);
            int sx = (SCREEN_W - 6 * TILE_W) / 2;
            draw_score_str(sx, 108, score_get(), GO_TITLE_COLOR);
        }

        if (show_timer > 180) {
            const char *press = "WCISNIJ ENTER";
            int px = (SCREEN_W - (int)(strlen(press) * TILE_W)) / 2;
            if ((show_timer / 30) % 2 == 0)
                video_draw_string(px, 141, press, GO_BLINK_COLOR);

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
