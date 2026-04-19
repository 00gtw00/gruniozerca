/*
 * title.c — Ekran tytułowy, ustawienia, kredyty
 * Gruniożerca DOS port, 2024
 *
 * Tła PCX:
 *   assets/menu.pcx     — scena z Grunio; menu overlay na kremowym panelu
 *   assets/settings.pcx — kremowy panel na full screen; ustawienia i kredyty
 *
 * Kolory tekstu: ciemne (idx z palety PCX) na kremowym tle panelu.
 */
#include "title.h"
#include "video.h"
#include "input.h"
#include "timer.h"
#include "sound.h"
#include "config.h"
#include "pcx.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ============================================================
   PCX tła — menu.pcx
   ============================================================ */
static uint8_t s_pcx_bg[SCREEN_W * SCREEN_H];
static uint8_t s_pcx_pal[768];
static int     s_pcx_loaded = 0;

static void pcx_palette_apply(void) {
    int i;
    if (!s_pcx_loaded) return;
    for (i = 0; i < 256; i++)
        video_set_color((uint8_t)i,
                        s_pcx_pal[i*3+0] >> 2,
                        s_pcx_pal[i*3+1] >> 2,
                        s_pcx_pal[i*3+2] >> 2);
}

static void ensure_menu_loaded(void) {
    if (!s_pcx_loaded &&
        pcx_load_buf("assets/menu.pcx", s_pcx_bg, sizeof(s_pcx_bg), s_pcx_pal) == PCX_OK)
        s_pcx_loaded = 1;
}

/* ============================================================
   PCX tła — settings.pcx
   ============================================================ */
static uint8_t s_set_bg[SCREEN_W * SCREEN_H];
static uint8_t s_set_pal[768];
static int     s_set_loaded = 0;

static void set_palette_apply(void) {
    int i;
    if (!s_set_loaded) return;
    for (i = 0; i < 256; i++)
        video_set_color((uint8_t)i,
                        s_set_pal[i*3+0] >> 2,
                        s_set_pal[i*3+1] >> 2,
                        s_set_pal[i*3+2] >> 2);
}

static void ensure_set_loaded(void) {
    if (!s_set_loaded &&
        pcx_load_buf("assets/settings.pcx", s_set_bg, sizeof(s_set_bg), s_set_pal) == PCX_OK)
        s_set_loaded = 1;
}

/* ============================================================
   Menu overlay na kremowym panelu menu.pcx
   Panel cream: x≈179..281 (w≈103), y≈94..145 (h≈51), center_x=230
   Tekst: ciemne kolory z palety menu.pcx
   ============================================================ */
#define MC_CURSOR_X   183   /* kursor ">" */
#define MC_TEXT_X     195   /* lewy brzeg tekstu */

/* Kolory z palety menu.pcx — ciemne na kremowym tle */
#define MCO_NORM    2    /* prawie czarny: normalna pozycja */
#define MCO_SEL    92    /* pomarańczowy: zaznaczona pozycja */
#define MCO_CUR    91    /* ciemny pomarańcz: kursor */

static const char * const menu_items[] = { "NOWA GRA", "USTAWIENIA", "AUTORZY" };
static const int          menu_item_y[]= { 104, 116, 128 };
#define MENU_COUNT 3

static void draw_menu(int sel) {
    int i;
    for (i = 0; i < MENU_COUNT; i++) {
        uint8_t col = (i == sel) ? MCO_SEL : MCO_NORM;
        if (i == sel)
            video_draw_string(MC_CURSOR_X, menu_item_y[i], ">", MCO_CUR);
        video_draw_string(MC_TEXT_X, menu_item_y[i], menu_items[i], col);
    }
}

/* ============================================================
   Ustawienia — tekst na kremowym panelu settings.pcx
   Panel inner: x=42..276 (w=235), y=33..154 (h=122)
   Tekst: ciemne kolory z palety settings.pcx
   ============================================================ */
/* Granice panelu settings.pcx */
#define SP_X    42
#define SP_Y    33
#define SP_W   235    /* 276-42+1 */
#define SP_H   122    /* 154-33+1 */

/* Wewnętrzne marginesy */
#define SP_LX   (SP_X + 14)    /* x etykiety */
#define SP_VX   (SP_X + 116)   /* x wartości — dopasowane do szerokości ramki */
#define SP_CX   (SP_X + 6)     /* x kursora ">" */
#define SP_ROW0 (SP_Y + 20)    /* y pierwszego wiersza treści */
#define SP_ROWH  14             /* wysokość wiersza */
#define SP_FOOTH (SP_Y + SP_H - 14)  /* y stopki */

/* Kolory z palety settings.pcx — ciemne na kremowym tle */
#define SCO_NORM    2    /* prawie czarny */
#define SCO_SEL    72    /* pomarańczowy: zaznaczony */
#define SCO_CUR    71    /* ciemny pomarańcz: kursor */
#define SCO_HEAD    5    /* ciemny brąz: nagłówek zakładki */
#define SCO_HINT   15    /* brązowy: podpowiedź */
#define SCO_TABON  72    /* aktywna zakładka */
#define SCO_TABOFF  2    /* nieaktywna zakładka */

static void draw_tab_bar(int active) {
    /* Dwie zakładki: rysujemy tylko etykiety, aktywna w kolorze pomarańczowym */
    uint8_t c0 = (active == 0) ? SCO_TABON  : SCO_TABOFF;
    uint8_t c1 = (active == 1) ? SCO_TABON  : SCO_TABOFF;
    video_draw_string(SP_LX,        SP_Y + 5, "F1:STEROWANIE", c0);
    video_draw_string(SP_LX + 116,  SP_Y + 5, "F2:DZWIEK",     c1);
    /* Pozioma linia pod zakładkami */
    video_fill_rect(SP_X + 4, SP_Y + 15, SP_W - 8, 1, SCO_NORM);
}

static void draw_set_row(int row, const char *label, const char *value, int sel) {
    int y = SP_ROW0 + row * SP_ROWH;
    uint8_t lc = sel ? SCO_SEL : SCO_NORM;
    uint8_t vc = sel ? SCO_SEL : SCO_HEAD;
    if (sel)
        video_draw_string(SP_CX, y + 2, ">", SCO_CUR);
    video_draw_string(SP_LX, y + 2, label, lc);
    if (value)
        video_draw_string(SP_VX, y + 2, value, vc);
}

static void draw_set_footer(const char *hint) {
    video_draw_string(SP_LX, SP_FOOTH + 2, hint, SCO_HINT);
}

/* Scancode → czytelna nazwa */
static const char *sc_label(uint8_t sc) {
    switch (sc) {
    case SC_LEFT:  return "LEWO       ";
    case SC_RIGHT: return "PRAWO      ";
    case SC_UP:    return "GORA       ";
    case SC_DOWN:  return "DOL        ";
    case SC_ENTER: return "ENTER      ";
    case SC_ESC:   return "ESCAPE     ";
    case SC_SPACE: return "SPACE      ";
    case SC_Z:     return "Z          ";
    case SC_X:     return "X          ";
    default: { static char b[12]; snprintf(b, sizeof(b), "SC:0x%02X    ", sc); return b; }
    }
}

static const char * const card_labels[] = {
    "BRAK      ", "PC SPEAKER", "ADLIB/OPL2",
    "OPL3      ", "SND BLSTR ", "SB PRO    ", "SB 16     "
};
#define NUM_CARDS 7

/* ============================================================
   Ustawienia — główna pętla
   ============================================================ */
static void settings_run(void) {
    int tab = 0, sel = 0, waiting_key = 0;

    ensure_set_loaded();

    while (1) {
        /* ---- Tło PCX ---- */
        memcpy(video_backbuf, s_set_bg, sizeof(s_set_bg));

        draw_tab_bar(tab);

        if (tab == 0) {
            /* Gdy oczekujemy na klawisz, pomijamy normalny wiersz zaznaczonej
             * pozycji — inaczej oba teksty nakładają się (transparentne piksele). */
            if (!(waiting_key && sel == 0)) draw_set_row(0, "LEWO:    ", sc_label(g_config.key_left),   sel==0);
            if (!(waiting_key && sel == 1)) draw_set_row(1, "PRAWO:   ", sc_label(g_config.key_right),  sel==1);
            if (!(waiting_key && sel == 2)) draw_set_row(2, "AKCJA:   ", sc_label(g_config.key_action), sel==2);
            if (!(waiting_key && sel == 3)) draw_set_row(3, "START:   ", sc_label(g_config.key_start),  sel==3);
            draw_set_row(4, "JOYSTICK:", g_config.use_joystick ? "WLACZONY  " : "WYLACZONY ", sel==4);
            if (waiting_key)
                draw_set_row(sel, "NACISNIJ KLAWISZ...", NULL, 1);
            draw_set_footer("ENTER = ZMIEN");
        } else {
            int ci = (int)g_config.sound.type;
            if (ci < 0 || ci >= NUM_CARDS) ci = 0;
            char mv[20], sv[20];
            int mf = g_config.sound.vol_music * 8 / 100;
            int sf = g_config.sound.vol_sfx   * 8 / 100;
            {
                int i; char *p;
                mv[0]='['; for(i=0,p=mv+1;i<8;i++) *p++=(i<mf)?'#':'-'; *p++=']';
                snprintf(p, (size_t)(mv+sizeof(mv)-p), "%3d%%", g_config.sound.vol_music);
                sv[0]='['; for(i=0,p=sv+1;i<8;i++) *p++=(i<sf)?'#':'-'; *p++=']';
                snprintf(p, (size_t)(sv+sizeof(sv)-p), "%3d%%", g_config.sound.vol_sfx);
            }
            draw_set_row(0, "KARTA:   ", card_labels[ci], sel==0);
            draw_set_row(1, "MUZYKA:  ", mv,              sel==1);
            draw_set_row(2, "SFX:     ", sv,              sel==2);
            draw_set_row(3, "TEST:    ", "[TEST DZWIEKU]", sel==3);
            draw_set_footer("<-/-> = ZMIEN  ENTER = TEST");
        }

        video_flip();
        sound_update();
        waitframe();
        input_update();

        if (keys_pressed[SC_F1] && tab != 0) { tab = 0; sel = 0; waiting_key = 0; continue; }
        if (keys_pressed[SC_F2] && tab != 1) { tab = 1; sel = 0; waiting_key = 0; continue; }

        if (keys_pressed[SC_ESC]) { config_save(); break; }

        int max_sel = (tab == 0) ? 4 : 3;
        if (!waiting_key) {
            if (keys_pressed[SC_UP])   sel = (sel > 0)       ? sel-1 : max_sel;
            if (keys_pressed[SC_DOWN]) sel = (sel < max_sel) ? sel+1 : 0;
        }

        if (tab == 0) {
            if (waiting_key) {
                for (int i = 1; i < KEY_COUNT; i++) {
                    if (keys_pressed[i]) {
                        switch (sel) {
                        case 0: g_config.key_left   = (uint8_t)i; break;
                        case 1: g_config.key_right  = (uint8_t)i; break;
                        case 2: g_config.key_action = (uint8_t)i; break;
                        case 3: g_config.key_start  = (uint8_t)i; break;
                        default: break;
                        }
                        config_apply();
                        waiting_key = 0;
                        break;
                    }
                }
            } else {
                if (keys_pressed[SC_ENTER]) {
                    if      (sel < 4) waiting_key = 1;
                    else if (sel == 4) g_config.use_joystick = !g_config.use_joystick;
                }
                if (keys_pressed[SC_LEFT] || keys_pressed[SC_RIGHT]) {
                    if (sel == 4) g_config.use_joystick = !g_config.use_joystick;
                }
            }
        } else {
            int d = keys_pressed[SC_RIGHT] ? 1 : keys_pressed[SC_LEFT] ? -1 : 0;
            if (d) {
                if (sel == 0) {
                    int nc = (int)g_config.sound.type;
                    nc = (nc + d + NUM_CARDS) % NUM_CARDS;
                    g_config.sound.type = (SoundCardType)nc;
                } else if (sel == 1) {
                    int v = (int)g_config.sound.vol_music + d * 10;
                    if (v < 0) v = 0; else if (v > 100) v = 100;
                    g_config.sound.vol_music = (uint8_t)v;
                    sound_set_vol_music(g_config.sound.vol_music);
                } else if (sel == 2) {
                    int v = (int)g_config.sound.vol_sfx + d * 10;
                    if (v < 0) v = 0; else if (v > 100) v = 100;
                    g_config.sound.vol_sfx = (uint8_t)v;
                    sound_set_vol_sfx(g_config.sound.vol_sfx);
                }
            }
            if (keys_pressed[SC_ENTER]) {
                if (sel == 0) {
                    SoundCardType det = sound_detect();
                    if (det != SND_NONE) g_config.sound = snd_config;
                } else if (sel == 3) {
                    sound_play_sfx(SFX_CATCH);
                }
            }
        }
    }
}

/* ============================================================
   Kredyty — tekst na kremowym panelu settings.pcx
   ============================================================ */
static void credits_run(void) {
    /* 13 linii × 8px = 104px; centrow. w SP_H=122 → top offset=9 */
    static const char * const lines[] = {
        "GRUNIOZERCA",
        "",
        "NES ORYGINAL:",
        "https://ar.hn/gruniozerca/",
        "",
        "WERSJA DOS:",
        "SEBASTIAN IZYCKI",
        "",
        "MUZYKA:",
        "OZZED (CC BY-SA 3.0)",
        "",
        "ENTER / ESC = WSTECZ"
    };
    const int NUM_LINES = (int)(sizeof(lines) / sizeof(lines[0]));
    const int LINE_H    = 8;
    const int total_h   = NUM_LINES * LINE_H;
    const int start_y   = SP_Y + (SP_H - total_h) / 2;

    ensure_set_loaded();
    memcpy(video_backbuf, s_set_bg, sizeof(s_set_bg));

    for (int i = 0; i < NUM_LINES; i++) {
        if (lines[i][0]) {
            uint8_t col = (i == 0) ? SCO_TABON :
                          (i == NUM_LINES-1) ? SCO_HINT : SCO_NORM;
            int tw  = (int)(strlen(lines[i]) * TILE_W);
            int lx  = SP_X + (SP_W - tw) / 2;
            if (lx < SP_LX) lx = SP_LX;
            video_draw_string(lx, start_y + i * LINE_H, lines[i], col);
        }
    }
    video_flip();

    while (1) {
        waitframe();
        input_update();
        sound_update();
        if (keys_pressed[SC_ESC] || keys_pressed[SC_ENTER] ||
            input_pressed(ACT_START) || input_pressed(ACT_ACTION))
            break;
    }
}

/* ============================================================
   Ekran "Jak grać?" — typewriter, settings.pcx background
   Pokazywany raz gdy hi score == 0 (pierwsze uruchomienie).
   ============================================================ */
void tutorial_run(void) {
    /* Dynamiczna linia z aktualnym klawiszem akcji */
    char action_line[32];
    {
        const char *sc = sc_label(g_config.key_action);
        char sc_buf[12];
        int i;
        strncpy(sc_buf, sc, 11); sc_buf[11] = '\0';
        /* Usuń padding spacji z prawej */
        for (i = (int)strlen(sc_buf) - 1; i > 0 && sc_buf[i] == ' '; i--)
            sc_buf[i] = '\0';
        snprintf(action_line, sizeof(action_line), "%s: ZMIEN KOLOR", sc_buf);
    }

    const char *lines[] = {
        "JAK GRAC?",
        "LAP MARCHEWKI KTORE",
        "MAJA KOLOR GRUNIO!",
        action_line,
        "LEWO / PRAWO: RUCH",
        "MASZ 6 ZYC - POWODZENIA!"
    };
    const int NUM_LINES = 6;
    /* Y-pozycje wierszy na tle settings.pcx */
    const int ly[] = { 53, 68, 78, 95, 105, 120 };
    const uint8_t lc[] = {
        SCO_TABON,  /* "JAK GRAC?" — pomarańczowy */
        SCO_NORM,
        SCO_NORM,
        SCO_SEL,    /* klawisz akcji — pomarańczowy */
        SCO_NORM,
        SCO_NORM
    };

    /* Suma wszystkich znaków (dla timeoutów typewritera) */
    int total_chars = 0;
    {
        int i;
        for (i = 0; i < NUM_LINES; i++) total_chars += (int)strlen(lines[i]);
    }

    ensure_set_loaded();
    set_palette_apply();

    video_clear();
    while (video_fade_step(1)) { waitframe(); video_flip(); }

    int chars_shown = 0;
    int type_timer  = 0;
    int done        = 0;
    uint32_t blink  = 0;

    while (1) {
        waitframe();
        input_update();
        sound_update();

        /* ENTER / ACTION: skip typewriter → potem wyjdź */
        if (keys_pressed[SC_ENTER] || input_pressed(ACT_ACTION)) {
            if (!done) {
                chars_shown = total_chars;
                done = 1;
            } else {
                break;
            }
        }

        /* Postęp typewritera: 1 znak co 2 klatki */
        if (!done) {
            type_timer++;
            if (type_timer >= 2) {
                type_timer = 0;
                if (++chars_shown >= total_chars) { chars_shown = total_chars; done = 1; }
            }
        }

        /* Tło */
        if (s_set_loaded)
            memcpy(video_backbuf, s_set_bg, sizeof(s_set_bg));
        else
            video_clear();

        /* Rysuj tekst (typewriter) */
        {
            int remaining = chars_shown;
            int i;
            for (i = 0; i < NUM_LINES; i++) {
                int len = (int)strlen(lines[i]);
                int draw_len;
                char buf[64];
                int lx;
                if (remaining <= 0) break;
                draw_len = (remaining >= len) ? len : remaining;
                strncpy(buf, lines[i], (size_t)draw_len);
                buf[draw_len] = '\0';
                /* Wiersz 0: wyśrodkuj względem pełnej długości */
                if (i == 0)
                    lx = SP_X + (SP_W - len * TILE_W) / 2;
                else
                    lx = SP_LX;
                video_draw_string(lx, ly[i], buf, lc[i]);
                remaining -= len;
            }
        }

        /* Stopka migająca po zakończeniu typewritera */
        if (done) {
            blink++;
            if ((blink / 30) % 2 == 0) {
                const char *foot = "ENTER = ZACZNIJ GRE";
                int fw = (int)(strlen(foot) * TILE_W);
                int fx = SP_X + (SP_W - fw) / 2;
                video_draw_string(fx, SP_FOOTH + 2, foot, SCO_HINT);
            }
        }

        video_flip();
    }

    while (video_fade_step(-1)) { waitframe(); video_flip(); }
}

/* ============================================================
   Główny ekran tytułowy
   ============================================================ */
TitleResult title_run(void) {
    int sel = 0;

    ensure_menu_loaded();
    pcx_palette_apply();
    sound_play_music(MUS_TITLE);

    /* Fade in */
    video_fade_reset();
    while (video_fade_step(1)) {
        waitframe();
        memcpy(video_backbuf, s_pcx_bg, sizeof(s_pcx_bg));
        draw_menu(sel);
        video_flip();
    }

    while (1) {
        waitframe();
        input_update();
        sound_update();

        if (keys_pressed[SC_UP])
            sel = (sel > 0) ? sel - 1 : MENU_COUNT - 1;
        if (keys_pressed[SC_DOWN])
            sel = (sel < MENU_COUNT - 1) ? sel + 1 : 0;

        if (keys_pressed[SC_ENTER] || input_pressed(ACT_START) || input_pressed(ACT_ACTION)) {
            if (sel == 0) {
                /* NEW GAME */
                sound_stop_music();
                while (video_fade_step(-1)) { waitframe(); video_flip(); }
                return TITLE_RESULT_START;
            } else if (sel == 1) {
                /* SETTINGS — przełącz na paletę settings.pcx */
                ensure_set_loaded();
                set_palette_apply();
                settings_run();
                /* Przywróć paletę menu + muzykę */
                pcx_palette_apply();
                sound_play_music(MUS_TITLE);
                continue;  /* wymuś nowy input_update — zapobiega ESC wychodzącemu z gry */
            } else {
                /* CREDITS */
                ensure_set_loaded();
                set_palette_apply();
                credits_run();
                pcx_palette_apply();
                continue;  /* j.w. */
            }
        }

        if (keys_pressed[SC_ESC]) {
            sound_stop_music();
            while (video_fade_step(-1)) { waitframe(); video_flip(); }
            return TITLE_RESULT_QUIT;
        }

        /* Rysowanie */
        memcpy(video_backbuf, s_pcx_bg, sizeof(s_pcx_bg));
        draw_menu(sel);
        video_flip();
    }
}
