/*
 * main.c — Punkt wejścia gry DOS, główna pętla, zarządzanie stanami
 * Gruniożerca DOS port, 2024
 *
 * Stany gry: TITLE → GAMEPLAY → GAMEOVER → HISCORE → TITLE
 * Port z nes/Sys/mainloop.asm i nes/Sys/title.asm.
 *
 * Kolejność inicjalizacji:
 *   mem_init → config_load → timer_init → input_init →
 *   video_init → sound_init → score_init → główna pętla
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dos_compat.h"
#include "memory.h"
#include "timer.h"
#include "video.h"
#include "input.h"
#include "player.h"
#include "carrot.h"
#include "score.h"
#include "sound.h"
#include "config.h"
#include "title.h"
#include "pcx.h"
#include "pack.h"

/* Deklaracje funkcji ekranowych */
void gameover_run(void);
void hiscore_run(void);

/* ---------- Tło: niebo + chmury ------------------------------------------- */
/* Gradient nieba: 6 pasów (220–225) + 2 kolory chmur (226–227)
 * UI_Y=168 / 6 = 28 px na pas; ciemny góra → jasny horyzont */
#define COL_SKY0      220   /* ciemnoniebieski — czubek ekranu   */
#define COL_SKY1      221
#define COL_SKY2      222
#define COL_SKY3      223
#define COL_SKY4      224
#define COL_SKY5      225   /* jasnoniebieski — horyzont          */
#define COL_CLOUD_HI  226   /* chmura: jasna biel (ciało)         */
#define COL_CLOUD_LO  227   /* chmura: szary cień (spód)          */

/* xfp: pozycja x jako fixed-point 8.8 (przesuń o 8 bitów, rysuj xfp>>8) */
typedef struct { int xfp, y, w; } Cloud;
#define NUM_CLOUDS     4
#define CLOUD_Y_CNT    8
#define CLOUD_SPEED_FP 128  /* 128/256 = 0.5 px/klatka = 1px/2 klatki ≈ 30 px/s */
static const int cloud_ytab[CLOUD_Y_CNT] = { 8, 22, 40, 14, 50, 28, 6, 35 };

static Cloud g_clouds[NUM_CLOUDS];
static int   g_cloud_yi[NUM_CLOUDS];

/* ---------- Tło PCX gameplay (assets/gameplay.pcx) ------------------------ */
/* Jeśli plik istnieje: jest używany zamiast gradientu nieba + chmur.
 * Paleta PCX nakładana na indeksy 64–255; sprite'y (0–63) nienaruszone. */
static uint8_t s_gplay_bg[SCREEN_H * SCREEN_W];
static uint8_t s_gplay_pal[768];
static int     s_gplay_loaded = 0;

static void ensure_gplay_loaded(void) {
    if (s_gplay_loaded) return;
    uint8_t raw_pal[768];
    PCXResult r = pcx_load_buf("assets/gameplay.pcx",
                                s_gplay_bg, sizeof(s_gplay_bg), raw_pal);
    if (r != PCX_OK) { s_gplay_loaded = -1; return; }

    /* Przesuń indeksy pikseli z zakresu 0–255 → 64–255 (mod 192),
     * by uniknąć konfliktu z paletą sprite'ów NES (indeksy 0–63). */
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        s_gplay_bg[i] = (uint8_t)(64 + (s_gplay_bg[i] % 192));

    /* Zbuduj tablicę palety: wpis n → nowy indeks 64+(n%192) */
    memset(s_gplay_pal, 0, sizeof(s_gplay_pal));
    for (int n = 0; n < 256; n++) {
        int ni = 64 + (n % 192);
        /* Wpisz tylko jeśli slot jeszcze wolny (zapobiega nadpisaniu przy aliasach) */
        if (!s_gplay_pal[ni*3] && !s_gplay_pal[ni*3+1] && !s_gplay_pal[ni*3+2]) {
            s_gplay_pal[ni*3+0] = raw_pal[n*3+0];
            s_gplay_pal[ni*3+1] = raw_pal[n*3+1];
            s_gplay_pal[ni*3+2] = raw_pal[n*3+2];
        }
    }
    s_gplay_loaded = 1;
}

/* Ustawia 8 kolorów palety dla nieba i chmur (VGA DAC, zakres 0–63) */
static void gameplay_set_sky_palette(void) {
    /* Gradient: ciemny granat (góra) → jasny błękit (horyzont) */
    video_set_color(COL_SKY0,  2,  4, 20);   /* prawie czarny — czubek ekranu */
    video_set_color(COL_SKY1,  5,  9, 27);
    video_set_color(COL_SKY2,  9, 16, 34);
    video_set_color(COL_SKY3, 14, 24, 41);
    video_set_color(COL_SKY4, 20, 34, 48);
    video_set_color(COL_SKY5, 27, 44, 55);   /* jasny widnokrąg */
    video_set_color(COL_CLOUD_HI, 62, 62, 63); /* czysta biel */
    video_set_color(COL_CLOUD_LO, 22, 22, 28); /* ciemny szaro-niebieski cień */
}

/* Rysuje gradient nieba: 6 poziomych pasów po 28 px */
static void draw_sky_gradient(void) {
    static const uint8_t sky_cols[6] = {
        COL_SKY0, COL_SKY1, COL_SKY2, COL_SKY3, COL_SKY4, COL_SKY5
    };
    for (int i = 0; i < 6; i++)
        video_fill_rect(0, i * 28, SCREEN_W, 28, sky_cols[i]);
}

/* Rysuje miękką chmurę z kilku zachodzących "bąbli" + cień na dole.
 * Kolejność: najpierw cień (niżej), potem białe ciało na wierzchu. */
static void draw_cloud_soft(int x, int y, int w) {
    /* Cień — szeroki ciemny pas pod spodem chmury (rysowany pierwszy = w tyle) */
    video_fill_rect(x + 2,       y + 16, w - 4,   7, COL_CLOUD_LO);
    /* Ciało — puchate bąble (rysowane na wierzchu cienia) */
    video_fill_rect(x,           y +  9, w,       10, COL_CLOUD_HI); /* podstawa */
    video_fill_rect(x + w/4,     y +  1, w/2,     12, COL_CLOUD_HI); /* środkowy bąbel */
    video_fill_rect(x + w/8,     y +  4, w*2/5,   10, COL_CLOUD_HI); /* lewy bąbel */
    video_fill_rect(x + w*11/20, y +  4, w*2/5,   10, COL_CLOUD_HI); /* prawy bąbel */
}

/* ---------- Typy stanów gry ----------------------------------------------- */
typedef enum {
    STATE_TITLE    = 0,
    STATE_GAMEPLAY = 1,
    STATE_GAMEOVER = 2,
    STATE_HISCORE  = 3,
    STATE_QUIT     = 4
} GameState;

/* ---------- Dane stanu gameplay ------------------------------------------- */
static Player    g_player;
static CarrotPool g_carrots;
static uint8_t   g_paused   = 0;
static uint8_t   g_lives    = 3;
static int       g_shake    = 0;   /* licznik klatek trzęsienia ekranu */
#define SHAKE_FRAMES  8            /* liczba klatek efektu */
#define SHAKE_AMT     3            /* amplituda przesunięcia (piksele) */

/* ---- Tilemap tła gameplay (40×25 kafelków) — wypełniamy podczas init ---- */
static Tile g_bg_tilemap[TILEMAP_ROWS][TILEMAP_COLS];

static void gameplay_init(void) {
    score_reset();
    player_init(&g_player);
    carrot_pool_init(&g_carrots);
    g_paused = 0;
    g_lives  = 6;
    g_shake  = 0;
    g_player.lives = 6;

    /* Przywróć oryginalną paletę NES (mogła być nadpisana przez PCX tytułu) */
    video_restore_game_palette();

    /* Tło: spróbuj załadować gameplay.pcx; jeśli brak — użyj gradientu */
    ensure_gplay_loaded();
    if (s_gplay_loaded == 1)
        video_overlay_raw_palette(s_gplay_pal, 64);
    /* Zawsze przywróć kolory nieba i chmur (226/227 mogły być nadpisane przez PCX) */
    gameplay_set_sky_palette();

    /* Inicjalizacja chmur */
    {
        static const int ix[]  = { PLAY_X_OFFSET + 24,  PLAY_X_OFFSET + 140,
                                    PLAY_X_OFFSET + 200, PLAY_X_OFFSET + 80  };
        static const int iw[]  = { 48, 36, 56, 32 };
        static const int iyi[] = {  0,  3,  1,  5 };
        for (int i = 0; i < NUM_CLOUDS; i++) {
            g_clouds[i].xfp = ix[i] << 8;
            g_clouds[i].y   = cloud_ytab[iyi[i]];
            g_clouds[i].w   = iw[i];
            g_cloud_yi[i]   = iyi[i];
        }
    }

    /* Tło z gameplay.nam: rząd podłogi + rząd dekoracji.
     * Tile IDs z NES nes/Gfx/nam/gameplay.nam:
     *   0x42 (66) = kafelek podłogi (wzór B/C)
     *   0x43 (67) = kafelek podłogi wariant (co ~6 kolumn)
     */
    memset(g_bg_tilemap, 0, sizeof(g_bg_tilemap));
    {
        int floor_row = (UI_Y / TILE_H) - 1;  /* wiersz 20 → y=160 przy UI_Y=168 */
        for (int col = 0; col < TILEMAP_COLS; col++) {
            uint8_t tid = ((col % 6) == 5) ? 0x43 : 0x42;
            g_bg_tilemap[floor_row][col] = (Tile){ .tile_id = tid, .palette = 0 };
        }
    }

    sound_play_music(MUS_INGAME);

    /* Fade in */
    while (video_fade_step(1)) {
        waitframe();
        if (s_gplay_loaded == 1) {
            memcpy(video_backbuf, s_gplay_bg, sizeof(video_backbuf));
        } else {
            video_clear();
            draw_sky_gradient();
        }
        for (int ci = 0; ci < NUM_CLOUDS; ci++)
            draw_cloud_soft(g_clouds[ci].xfp >> 8, g_clouds[ci].y, g_clouds[ci].w);
        video_draw_tilemap(g_bg_tilemap);
        video_flip();
    }
}

static GameState gameplay_run(void) {
    while (1) {
        waitframe();
        input_update();
        sound_update();

        /* ---- ESC — dialog wyjścia ---- */
        if (keys_pressed[SC_ESC]) {
            /* Narysuj dialog na bieżącej klatce */
            video_fill_rect(84, 76, 152, 28, COL_BLACK);
            video_draw_string(112, 82,  "ZAMKNAC GRE?", 2);
            video_draw_string(88,  94,  "ENTER=TAK  ESC=NIE", 2);
            video_flip();
            /* Czekaj na odpowiedź — ESC=anuluj, ENTER=wyjdź */
            while (1) {
                waitframe();
                input_update();
                if (keys_pressed[SC_ENTER]) {
                    sound_stop_music();
                    while (video_fade_step(-1)) { waitframe(); video_flip(); }
                    return STATE_QUIT;
                }
                if (keys_pressed[SC_ESC]) break;
            }
            continue;
        }

        /* ---- Pauza ---- */
        if (input_pressed(ACT_START)) {
            g_paused = !g_paused;
            if (g_paused) {
                const char *ptxt = "  PAUZA  ";
                int px = SCREEN_W / 2 - (int)(strlen(ptxt) * TILE_W) / 2;
                video_draw_string(px, SCREEN_H / 2, ptxt, 2);
                video_flip();
                continue;
            }
        }
        if (g_paused) continue;

        /* ---- Logika ---- */
        uint32_t score = score_get();
        player_update(&g_player);
        carrot_pool_update(&g_carrots, player_x(&g_player), g_player.y,
                           g_player.color, &score);
        if (score != score_get()) score_add(score - score_get());
        if (carrot_had_catch()) player_notify_catch(&g_player);
        if (carrot_had_miss()) {
            if (g_shake == 0) g_shake = SHAKE_FRAMES;
            if (g_player.lives > 0) g_player.lives--;
        }

        /* ---- Rysowanie tła ---- */
        if (s_gplay_loaded == 1) {
            /* Tło PCX — kopiuj cały bufor, potem rysuj game objects na wierzchu */
            memcpy(video_backbuf, s_gplay_bg, sizeof(video_backbuf));
        } else {
            /* Fallback: gradient nieba */
            video_clear();
            draw_sky_gradient();
        }

        /* Chmury — zawsze nad tłem (PCX lub gradient), ruch co klatkę */
        for (int ci = 0; ci < NUM_CLOUDS; ci++) {
            g_clouds[ci].xfp -= CLOUD_SPEED_FP;
            int cx = g_clouds[ci].xfp >> 8;
            if (cx + g_clouds[ci].w + 4 < 0) {
                g_cloud_yi[ci] = (g_cloud_yi[ci] + NUM_CLOUDS + 1) % CLOUD_Y_CNT;
                g_clouds[ci].y   = cloud_ytab[g_cloud_yi[ci]];
                g_clouds[ci].xfp = (SCREEN_W + 4) << 8;
            }
        }
        for (int ci = 0; ci < NUM_CLOUDS; ci++)
            draw_cloud_soft(g_clouds[ci].xfp >> 8, g_clouds[ci].y, g_clouds[ci].w);

        video_draw_tilemap(g_bg_tilemap);
        carrot_pool_draw(&g_carrots);
        player_draw(&g_player);

        /* Pasek UI — czarne tło od UI_Y do dołu ekranu (margines CRT) */
        video_fill_rect(0, UI_Y, SCREEN_W, SCREEN_H - UI_Y, COL_BLACK);
        score_draw_ui();
        score_draw_hi_ui();

        /* Serca życia — na górze ekranu z marginesem (tile 0x26 = ikona serca z NES) */
        for (int i = 0; i < g_player.lives; i++) {
            video_draw_sprite(PLAY_X_OFFSET + i * (TILE_W + 2), 8, 0x26, 0, 0);
        }

        /* ---- Trzęsienie ekranu ---- */
        if (g_shake > 0) {
            int d = (g_shake % 2 == 1) ? SHAKE_AMT : -SHAKE_AMT;
            int play_rows = UI_Y;   /* tylko obszar gry, bez paska UI */
            if (d > 0) {
                /* Przesuń obszar gry w górę o d px */
                memmove(video_backbuf,
                        video_backbuf + d * SCREEN_W,
                        (play_rows - d) * SCREEN_W);
                memset(video_backbuf + (play_rows - d) * SCREEN_W,
                       COL_BLACK, (size_t)(d * SCREEN_W));
            } else {
                /* Przesuń obszar gry w dół o |d| px */
                int ad = -d;
                memmove(video_backbuf + ad * SCREEN_W,
                        video_backbuf,
                        (play_rows - ad) * SCREEN_W);
                memset(video_backbuf, COL_BLACK, (size_t)(ad * SCREEN_W));
            }
            g_shake--;
        }

        video_flip();

        /* ---- Koniec gry ---- */
        if (g_player.lives == 0) {
            sound_stop_music();
            while (video_fade_step(-1)) { waitframe(); video_flip(); }
            return STATE_GAMEOVER;
        }
    }
}

/* ---------- Shutdown -------- -------------------------------------------- */
static void shutdown_all(void) {
    sound_shutdown();
    input_shutdown();
    video_shutdown();
    timer_shutdown();
    pack_shutdown();
}

/* ---------- main ---------------------------------------------------------- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* 1. Archiwum assetów — ładuj do RAM (jeśli GRUNIO.DAT istnieje) */
    pack_init();   /* cicha awaryjność: jeśli brak pliku, ładuje assets z dysku */

    /* 2. Pamięć */
    mem_init();

    /* 2. Konfiguracja */
    config_load();

    /* 3. Timer (60 Hz) */
    timer_init();

    /* 4. Input */
    input_init();
    config_apply();

    /* 5. Video */
    video_init("assets/palette.dat");
    video_load_sprites("assets/sprites.dat");

    /* 6. Dźwięk */
    SoundCardType detected = sound_detect();
    if (detected == SND_NONE)
        sound_init(&g_config.sound);
    else
        sound_init(&snd_config);

    /* 7. Wynik */
    score_init();

    /* ======= Główna pętla stanów ======= */
    GameState state = STATE_TITLE;

    while (state != STATE_QUIT) {
        switch (state) {

        case STATE_TITLE: {
            TitleResult r = title_run();
            state = (r == TITLE_RESULT_START) ? STATE_GAMEPLAY : STATE_QUIT;
            break;
        }

        case STATE_GAMEPLAY:
            if (score_get_hi() == 0) tutorial_run();
            gameplay_init();
            state = gameplay_run();
            break;

        case STATE_GAMEOVER:
            gameover_run();
            state = score_check_hi() ? STATE_HISCORE : STATE_TITLE;
            break;

        case STATE_HISCORE:
            hiscore_run();
            state = STATE_TITLE;
            break;

        default:
            state = STATE_QUIT;
            break;
        }
    }

    shutdown_all();

    puts(
"\n"
"  \xB0\xDB\xDF\xDF\xB0\xDB\xDF\xDC\xB0\xDB\xB0\xDB\xB0\xDB\xDF\xDB\xB0\xDF\xDB\xDF\xB0\xDB\xDF\xDB\xB0\xDF\xDF\xDB\xB0\xDB\xDF\xDF\xB0\xDB\xDF\xDC\xB0\xDB\xDF\xDF\xB0\xDB\xDF\xDB\n"
"  \xB0\xDB\xB0\xDB\xB0\xDB\xDF\xDC\xB0\xDB\xB0\xDB\xB0\xDB\xB0\xDB\xB0\xB0\xDB\xB0\xB0\xDB\xB0\xDB\xB0\xDC\xDF\xB0\xB0\xDB\xDF\xDF\xB0\xDB\xDF\xDC\xB0\xDB\xB0\xB0\xB0\xDB\xDF\xDB\n"
"  \xB0\xDF\xDF\xDF\xB0\xDF\xB0\xDF\xB0\xDF\xDF\xDF\xB0\xDF\xB0\xDF\xB0\xDF\xDF\xDF\xB0\xDF\xDF\xDF\xB0\xDF\xDF\xDF\xB0\xDF\xDF\xDF\xB0\xDF\xB0\xDF\xB0\xDF\xDF\xDF\xB0\xDF\xB0\xDF\n"
"\n"
"  Sebastian Izycki, 2026\n"
"  sebastian@izycki.pl\n"
"\n"
"  https://ar.hn/gruniozerca/\n"
    );

    return 0;
}
