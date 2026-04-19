/*
 * setup.c — SETUP.EXE: konfigurator TUI 80×25, tryb tekstowy INT 10h
 * Gruniożerca DOS port, 2024
 *
 * Kompilacja: make setup → build/SETUP.EXE
 * Wygląd: klasyczny niebieski DOS TUI z czerwonymi ramkami.
 * Nawigacja: strzałki, Enter, ESC, F1-F4.
 */
#include "config.h"
#include "input.h"
#include "sound.h"
#include "dos_compat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef SETUP_BUILD
#define SETUP_BUILD
#endif

/* =========================================================================
   Atrybuty kolorów — schemat niebieski/czerwony (klasyczny DOS TUI)
   ========================================================================= */
#define ATTR_NORMAL    0x1F  /* jasny biały na niebieskim */
#define ATTR_TITLE     0x4F  /* jasny biały na czerwonym  (pasek nagłówka/stopki) */
#define ATTR_HEADER    0x4F  /* alias do ATTR_TITLE */
#define ATTR_SELECTED  0x70  /* czarny na jasnym szarym   (zaznaczony element) */
#define ATTR_LABEL     0x1E  /* jasny żółty na niebieskim */
#define ATTR_VALUE     0x1B  /* jasny cyjan na niebieskim */
#define ATTR_KEY       0x1E  /* jasny żółty na niebieskim (skróty klawiszy) */
#define ATTR_BORDER    0x1C  /* jasny czerwony na niebieskim (ramki) */
#define ATTR_HELP      0x17  /* biały na niebieskim */
#define ATTR_DIM       0x18  /* ciemnoszary na niebieskim */

/* Znaki ramek CP437 */
#define BOX_TL   '\xC9'  /* ╔ */
#define BOX_TR   '\xBB'  /* ╗ */
#define BOX_BL   '\xC8'  /* ╚ */
#define BOX_BR   '\xBC'  /* ╝ */
#define BOX_H    '\xCD'  /* ═ */
#define BOX_V    '\xBA'  /* ║ */
#define BOX_ML   '\xCC'  /* ╠ */
#define BOX_MR   '\xB9'  /* ╣ */
#define BOX_SEP  '\xC4'  /* ─ (cienka linia separatora) */

#define SCR_COLS 80
#define SCR_ROWS 25

/* =========================================================================
   BIOS wrappers (INT 10h / INT 16h)
   ========================================================================= */
#ifdef DOS_BUILD
static void bios_gotoxy(uint8_t col, uint8_t row) {
    __dpmi_regs r;
    r.h.ah = 0x02; r.h.bh = 0x00;
    r.h.dh = row;  r.h.dl = col;
    __dpmi_int(0x10, &r);
}

static void bios_putchar_attr(uint8_t col, uint8_t row, char c, uint8_t attr) {
    __dpmi_regs r;
    r.h.ah = 0x02; r.h.bh = 0; r.h.dh = row; r.h.dl = col;
    __dpmi_int(0x10, &r);
    r.h.ah = 0x09; r.h.al = (uint8_t)c;
    r.h.bh = 0;    r.h.bl = attr; r.x.cx = 1;
    __dpmi_int(0x10, &r);
}

static void bios_cursor_hide(void) {
    __dpmi_regs r; r.h.ah = 0x01; r.x.cx = 0x2000;
    __dpmi_int(0x10, &r);
}
static void bios_cursor_show(void) {
    __dpmi_regs r; r.h.ah = 0x01; r.x.cx = 0x0607;
    __dpmi_int(0x10, &r);
}

static void bios_clear_screen(uint8_t attr) {
    __dpmi_regs r;
    r.h.ah = 0x06; r.h.al = 0; r.h.bh = attr;
    r.x.cx = 0x0000;
    r.h.dh = SCR_ROWS - 1; r.h.dl = SCR_COLS - 1;
    __dpmi_int(0x10, &r);
}

static uint8_t bios_readkey(void) {
    __dpmi_regs r;
    r.h.ah = 0x00;
    __dpmi_int(0x16, &r);
    if (r.h.al == 0) return r.h.ah | 0x80;
    return r.h.al;
}

#define KEY_UP    (0x48 | 0x80)
#define KEY_DOWN  (0x50 | 0x80)
#define KEY_LEFT  (0x4B | 0x80)
#define KEY_RIGHT (0x4D | 0x80)
#define KEY_F1    (0x3B | 0x80)
#define KEY_F2    (0x3C | 0x80)
#define KEY_F3    (0x3D | 0x80)
#define KEY_F4    (0x3E | 0x80)
#define KEY_ENTER  '\r'
#define KEY_ESC    '\x1B'
#define KEY_TAB    '\t'

#else /* host build — stubs */
static void bios_gotoxy(uint8_t c, uint8_t r)                          { (void)c;(void)r; }
static void bios_putchar_attr(uint8_t c,uint8_t r,char ch,uint8_t a)  { (void)c;(void)r;(void)ch;(void)a; }
static void bios_cursor_hide(void)        {}
static void bios_cursor_show(void)        {}
static void bios_clear_screen(uint8_t a) { (void)a; }
static uint8_t bios_readkey(void)        { return 0x1B; }
#define KEY_UP    0xC8
#define KEY_DOWN  0xD0
#define KEY_LEFT  0xCB
#define KEY_RIGHT 0xCD
#define KEY_F1    0xBB
#define KEY_F2    0xBC
#define KEY_F3    0xBD
#define KEY_F4    0xBE
#define KEY_ENTER '\r'
#define KEY_ESC   '\x1B'
#define KEY_TAB   '\t'
#endif

/* =========================================================================
   Helpery rysowania
   ========================================================================= */

static void put_str(uint8_t col, uint8_t row, const char *s, uint8_t attr) {
    while (*s) {
        bios_putchar_attr(col++, row, *s++, attr);
        if (col >= SCR_COLS) break;
    }
}

static void put_char(uint8_t col, uint8_t row, char c, uint8_t attr) {
    bios_putchar_attr(col, row, c, attr);
}

static void fill_row(uint8_t row, char c, uint8_t attr) {
    for (int i = 0; i < SCR_COLS; i++)
        bios_putchar_attr((uint8_t)i, row, c, attr);
}

static void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t attr) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            bios_putchar_attr((uint8_t)(x+c), (uint8_t)(y+r), ' ', attr);
}

/* Centrowanie tekstu w wierszu (zakres x..x+w) */
static void put_str_centered(uint8_t x, uint8_t y, uint8_t w,
                              const char *s, uint8_t attr) {
    int len = (int)strlen(s);
    int off = (w - len) / 2;
    if (off < 0) off = 0;
    put_str((uint8_t)(x + off), y, s, attr);
}

/* Podwójna ramka — wnętrze wypełnione ATTR_NORMAL (niebieski) */
static void draw_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t border_attr) {
    /* Wypełnij wnętrze */
    if (w > 2 && h > 2)
        fill_rect((uint8_t)(x+1), (uint8_t)(y+1),
                  (uint8_t)(w-2), (uint8_t)(h-2), ATTR_NORMAL);
    /* Górna krawędź */
    put_char(x, y, BOX_TL, border_attr);
    for (int i = 1; i < w-1; i++) put_char((uint8_t)(x+i), y, BOX_H, border_attr);
    put_char((uint8_t)(x+w-1), y, BOX_TR, border_attr);
    /* Boki */
    for (int r = 1; r < h-1; r++) {
        put_char(x, (uint8_t)(y+r), BOX_V, border_attr);
        put_char((uint8_t)(x+w-1), (uint8_t)(y+r), BOX_V, border_attr);
    }
    /* Dolna krawędź */
    put_char(x, (uint8_t)(y+h-1), BOX_BL, border_attr);
    for (int i = 1; i < w-1; i++) put_char((uint8_t)(x+i),(uint8_t)(y+h-1), BOX_H, border_attr);
    put_char((uint8_t)(x+w-1),(uint8_t)(y+h-1), BOX_BR, border_attr);
}

/* Tytuł wmontowany w górną krawędź ramki: ╔══ TYTUŁ ══╗ */
static void draw_box_titled(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                             const char *title, uint8_t border_attr) {
    draw_box(x, y, w, h, border_attr);
    if (title && title[0]) {
        char buf[SCR_COLS];
        int len = (int)strlen(title);
        int off = (w - len - 2) / 2;
        if (off < 1) off = 1;
        /* nadpisz fragment górnej krawędzi tytułem */
        put_char((uint8_t)(x + off), y, ' ', border_attr);
        for (int i = 0; i < len; i++)
            put_char((uint8_t)(x + off + 1 + i), y, title[i], ATTR_LABEL);
        put_char((uint8_t)(x + off + len + 1), y, ' ', border_attr);
        (void)buf;
    }
}

/* =========================================================================
   Pasek nagłówka (wiersz 0) i stopki (wiersz 24) — wspólne
   ========================================================================= */
static void draw_chrome(const char *tab_name) {
    /* Nagłówek */
    fill_row(0, ' ', ATTR_TITLE);
    put_str(2, 0, "GRUNIOZERCA v1.0", ATTR_TITLE);
    put_str_centered(0, 0, SCR_COLS, "SETUP / KONFIGURACJA", ATTR_TITLE);
    if (tab_name) {
        char rbuf[24];
        snprintf(rbuf, sizeof(rbuf), "%s ", tab_name);
        int len = (int)strlen(rbuf);
        put_str((uint8_t)(SCR_COLS - 1 - len), 0, rbuf, ATTR_TITLE);
    }

    /* Zakładki F1-F4 w wierszu 1 */
    fill_row(1, ' ', ATTR_NORMAL);
    put_str( 1, 1, " F1:Sterowanie ", ATTR_KEY);
    put_str(17, 1, " F2:Dzwiek ",     ATTR_KEY);
    put_str(28, 1, " F3:Testy ",      ATTR_KEY);
    put_str(38, 1, " F4:Domyslne ",   ATTR_KEY);
    put_str(52, 1, " ESC:Wyjscie ",   ATTR_KEY);

    /* Aktywna zakładka — podświetl */
    if (tab_name) {
        if (strcmp(tab_name, "STEROWANIE") == 0)
            put_str(1,  1, " F1:Sterowanie ", ATTR_SELECTED);
        else if (strcmp(tab_name, "DZWIEK") == 0)
            put_str(17, 1, " F2:Dzwiek ",     ATTR_SELECTED);
        else if (strcmp(tab_name, "TESTY") == 0)
            put_str(28, 1, " F3:Testy ",      ATTR_SELECTED);
    }

    /* Stopka */
    fill_row(SCR_ROWS-1, ' ', ATTR_TITLE);
    put_str(2, SCR_ROWS-1,
            "Strzalki:nawigacja   Enter:wybierz   Lewo/Prawo:zmiana wartosci",
            ATTR_TITLE);
}

/* =========================================================================
   EKRAN 1: STEROWANIE
   ========================================================================= */

static const char *sc_name(uint8_t sc) {
    switch (sc) {
    case SC_LEFT:  return "Strzalka L   ";
    case SC_RIGHT: return "Strzalka P   ";
    case SC_UP:    return "Strzalka G   ";
    case SC_DOWN:  return "Strzalka D   ";
    case SC_ENTER: return "Enter        ";
    case SC_ESC:   return "Escape       ";
    case SC_Z:     return "Z            ";
    case SC_X:     return "X            ";
    case SC_SPACE: return "Spacja       ";
    default: {
        static char buf[16];
        snprintf(buf, sizeof(buf), "SC:0x%02X       ", sc);
        return buf;
    }
    }
}

static void screen_input(void) {
    int sel = 0, max_sel = 5;
    int waiting_key = 0;

    while (1) {
        bios_clear_screen(ATTR_NORMAL);
        draw_chrome("STEROWANIE");

        draw_box_titled(1, 2, 46, 10, "KLAWIATURA", ATTR_BORDER);
        draw_box_titled(1, 13, 46, 6,  "JOYSTICK",   ATTR_BORDER);
        draw_box_titled(1, 19, 46, 4,  "MYSZ SERIAL",ATTR_BORDER);

        /* Skróty nawigacji po prawej */
        draw_box_titled(49, 2, 30, 21, "Pomoc", ATTR_BORDER);
        put_str(51,  4, "Enter = zmien binding",  ATTR_HELP);
        put_str(51,  5, "Esc/F1 = wyjscie",        ATTR_HELP);
        put_str(51,  7, "Dostepne klawisze:",      ATTR_LABEL);
        put_str(51,  8, "Strzalki, Enter,",        ATTR_DIM);
        put_str(51,  9, "Esc, Space, Z, X",        ATTR_DIM);

        const char *key_labels[] = { "Lewo  :", "Prawo :", "Akcja :", "Start :" };
        uint8_t *key_ptrs[] = {
            &g_config.key_left, &g_config.key_right,
            &g_config.key_action, &g_config.key_start
        };
        for (int i = 0; i < 4; i++) {
            put_str(3, (uint8_t)(3+i*2), key_labels[i], ATTR_LABEL);
            char vbuf[24];
            snprintf(vbuf, sizeof(vbuf), "[%-13s]", sc_name(*key_ptrs[i]));
            put_str(11, (uint8_t)(3+i*2), vbuf, (sel == i) ? ATTR_SELECTED : ATTR_VALUE);
        }

        /* Joystick */
        put_str(3, 14, "Joystick:", ATTR_LABEL);
        char joy_buf[20];
        snprintf(joy_buf, sizeof(joy_buf), "[%-9s]",
                 g_config.use_joystick ? "WLACZONY " : "WYLACZONY");
        put_str(13, 14, joy_buf, (sel == 4) ? ATTR_SELECTED : ATTR_VALUE);

        /* Mysz COM */
        put_str(3, 20, "Mysz COM: ", ATTR_LABEL);
        char com_buf[20];
        snprintf(com_buf, sizeof(com_buf), "[COM%d (0x%X)]",
                 g_config.mouse_com,
                 g_config.mouse_com == 1 ? 0x3F8 : 0x2F8);
        put_str(13, 20, com_buf, (sel == 5) ? ATTR_SELECTED : ATTR_VALUE);

        if (waiting_key) {
            fill_row(SCR_ROWS-2, ' ', ATTR_SELECTED);
            put_str(2, SCR_ROWS-2, " Nacisnij nowy klawisz... ", ATTR_SELECTED);
        }

        uint8_t k = bios_readkey();

        if (waiting_key) {
            uint8_t sc = (k & 0x80) ? (k & 0x7F) : k;
            *key_ptrs[sel] = sc;
            waiting_key = 0;
            continue;
        }

        if      (k == KEY_UP)    sel = (sel > 0) ? sel-1 : max_sel;
        else if (k == KEY_DOWN)  sel = (sel < max_sel) ? sel+1 : 0;
        else if (k == KEY_ENTER) {
            if (sel < 4)       waiting_key = 1;
            else if (sel == 4) g_config.use_joystick = !g_config.use_joystick;
            else if (sel == 5) {
                g_config.mouse_com = (g_config.mouse_com == 1) ? 2 : 1;
                g_config.use_mouse = 1;
            }
        }
        else if (k == KEY_ESC || k == KEY_F1) break;
    }
    config_save();
}

/* =========================================================================
   EKRAN 2: DŹWIĘK
   ========================================================================= */

/*
 * Elementy nawigacji:
 *  0 = typ karty        (Enter = cykl, L/R = cykl)
 *  1 = autodetekcja     (Enter = wykryj)
 *  2 = port I/O         (L/R = zmiana)
 *  3 = IRQ              (L/R = zmiana)
 *  4 = DMA              (L/R = zmiana)
 *  5 = głośność muzyki  (L/R = +/-10)
 *  6 = głośność SFX     (L/R = +/-10)
 *  7 = TEST DZWIEKU     (Enter = odegraj)
 *  8 = TEST MUZYKI      (Enter = odegraj)
 */
static const char *card_names[] = {
    "Brak       ", "PC Speaker ", "AdLib/OPL2 ", "OPL3       ",
    "Sound Blast", "SB Pro     ", "SB 16      "
};
#define NUM_CARDS 7

static void sound_screen_draw(int sel, const char *status) {
    bios_clear_screen(ATTR_NORMAL);
    draw_chrome("DZWIEK");

    /* Lewa kolumna: karta i parametry */
    draw_box_titled(1, 2, 46, 13, "KARTA DZWIEKOWA", ATTR_BORDER);

    int card_idx = (int)g_config.sound.type;
    if (card_idx < 0 || card_idx >= NUM_CARDS) card_idx = 0;

    /* Wiersz 0: typ karty */
    put_str(3,  4, "Karta:    ", ATTR_LABEL);
    char cbuf[20]; snprintf(cbuf, sizeof(cbuf), "[%-11s]", card_names[card_idx]);
    put_str(13, 4, cbuf, (sel==0) ? ATTR_SELECTED : ATTR_VALUE);
    put_str(27, 4, " L/R=zmien ", ATTR_HELP);

    /* Wiersz 1: autodetekcja — osobny przycisk */
    put_str(3,  6, "Autodetekcja:", ATTR_LABEL);
    put_str(17, 6, "[ Wykryj karte ]",
            (sel==1) ? ATTR_SELECTED : ATTR_KEY);

    /* Wiersz 2-4: port, irq, dma */
    char pbuf[16]; snprintf(pbuf, sizeof(pbuf), "[ 0x%-4X]", g_config.sound.port);
    put_str(3,  8, "Port I/O: ", ATTR_LABEL);
    put_str(13, 8, pbuf, (sel==2) ? ATTR_SELECTED : ATTR_VALUE);

    char ibuf[12]; snprintf(ibuf, sizeof(ibuf), "[ IRQ %d ]", g_config.sound.irq);
    put_str(3,  9, "IRQ:      ", ATTR_LABEL);
    put_str(13, 9, ibuf, (sel==3) ? ATTR_SELECTED : ATTR_VALUE);

    char dbuf[12]; snprintf(dbuf, sizeof(dbuf), "[ DMA %d ]", g_config.sound.dma);
    put_str(3,  10, "DMA:      ", ATTR_LABEL);
    put_str(13, 10, dbuf, (sel==4) ? ATTR_SELECTED : ATTR_VALUE);

    /* Głośność */
    draw_box_titled(1, 15, 46, 7, "GLOSNOSC", ATTR_BORDER);

    /* Pasek głośności */
    for (int vi = 0; vi < 2; vi++) {
        int vol   = (vi == 0) ? g_config.sound.vol_music : g_config.sound.vol_sfx;
        uint8_t y = (uint8_t)(17 + vi*2);
        put_str(3, y, (vi==0) ? "Muzyka: " : "SFX:    ", ATTR_LABEL);
        char bar[18];
        int filled = vol * 10 / 100;
        bar[0] = '[';
        for (int i = 0; i < 10; i++) bar[i+1] = (i < filled) ? '\xDB' : '\xB0';
        bar[11] = ']';
        snprintf(bar+12, sizeof(bar)-12, " %3d%%", vol);
        put_str(11, y, bar, (sel == (5+vi)) ? ATTR_SELECTED : ATTR_VALUE);
        put_str(28, y, (sel == (5+vi)) ? " L/R=zmiana " : "            ", ATTR_HELP);
    }

    /* Testy */
    draw_box_titled(49, 2, 30, 6, "Demo dzwieku", ATTR_BORDER);
    put_str(51, 4, "[ TEST DZWIEKU ]",
            (sel==7) ? ATTR_SELECTED : ATTR_KEY);
    put_str(51, 5, "[ TEST MUZYKI  ]",
            (sel==8) ? ATTR_SELECTED : ATTR_KEY);
    put_str(51, 7, "Enter = uruchom", ATTR_HELP);

    /* Prawy panel — wynik autodetekcji / status */
    draw_box_titled(49, 9, 30, 13, "Status", ATTR_BORDER);
    if (status && status[0])
        put_str(51, 11, status, ATTR_VALUE);
    else {
        put_str(51, 11, "Obecna konfiguracja:", ATTR_HELP);
        put_str(51, 12, card_names[card_idx],    ATTR_VALUE);
        char sbuf[20];
        snprintf(sbuf, sizeof(sbuf), "Port: 0x%04X", g_config.sound.port);
        put_str(51, 13, sbuf, ATTR_VALUE);
        snprintf(sbuf, sizeof(sbuf), "IRQ: %d  DMA: %d",
                 g_config.sound.irq, g_config.sound.dma);
        put_str(51, 14, sbuf, ATTR_VALUE);
    }
}

static void screen_sound(void) {
    int sel = 0, max_sel = 8;
    char status[64] = "";

    static const uint16_t ports_sb[]  = { 0x220, 0x240, 0x260, 0x280 };
    static const uint16_t ports_opl[] = { 0x388, 0x38A };
    static const uint8_t  irqs_sb[]   = { 5, 7, 2, 10 };
    static const uint8_t  dmas_sb[]   = { 1, 3 };

    while (1) {
        sound_screen_draw(sel, status);
        status[0] = '\0';  /* wyczyść status po narysowaniu */

        uint8_t k = bios_readkey();

        if      (k == KEY_UP)   sel = (sel > 0)       ? sel-1 : max_sel;
        else if (k == KEY_DOWN) sel = (sel < max_sel) ? sel+1 : 0;
        else if (k == KEY_LEFT || k == KEY_RIGHT) {
            int d = (k == KEY_RIGHT) ? 1 : -1;
            int card_idx = (int)g_config.sound.type;

            if (sel == 0) {
                card_idx = (card_idx + d + NUM_CARDS) % NUM_CARDS;
                g_config.sound.type = (SoundCardType)card_idx;
                switch (g_config.sound.type) {
                case SND_SB: case SND_SB16: case SND_SB_PRO:
                    g_config.sound.port = 0x220;
                    g_config.sound.irq  = 5;
                    g_config.sound.dma  = 1;
                    break;
                case SND_OPL2: case SND_OPL3:
                    g_config.sound.port = 0x388;
                    break;
                case SND_SPEAKER:
                    g_config.sound.port = 0x61;
                    break;
                default: break;
                }
            } else if (sel == 2) {
                /* Cykl portów */
                if (g_config.sound.type == SND_SB ||
                    g_config.sound.type == SND_SB16 ||
                    g_config.sound.type == SND_SB_PRO) {
                    uint8_t pi = 0;
                    for (uint8_t i = 0; i < 4; i++)
                        if (ports_sb[i] == g_config.sound.port) { pi = i; break; }
                    pi = (uint8_t)((pi + d + 4) % 4);
                    g_config.sound.port = ports_sb[pi];
                } else {
                    uint8_t pi = 0;
                    for (uint8_t i = 0; i < 2; i++)
                        if (ports_opl[i] == g_config.sound.port) { pi = i; break; }
                    pi = (uint8_t)((pi + d + 2) % 2);
                    g_config.sound.port = ports_opl[pi];
                }
            } else if (sel == 3) {
                uint8_t ri = 0;
                for (uint8_t i = 0; i < 4; i++)
                    if (irqs_sb[i] == g_config.sound.irq) { ri = i; break; }
                ri = (uint8_t)((ri + d + 4) % 4);
                g_config.sound.irq = irqs_sb[ri];
            } else if (sel == 4) {
                uint8_t di = 0;
                for (uint8_t i = 0; i < 2; i++)
                    if (dmas_sb[i] == g_config.sound.dma) { di = i; break; }
                di = (uint8_t)((di + d + 2) % 2);
                g_config.sound.dma = dmas_sb[di];
            } else if (sel == 5) {
                int v = (int)g_config.sound.vol_music + d * 10;
                if (v < 0) v = 0; else if (v > 100) v = 100;
                g_config.sound.vol_music = (uint8_t)v;
            } else if (sel == 6) {
                int v = (int)g_config.sound.vol_sfx + d * 10;
                if (v < 0) v = 0; else if (v > 100) v = 100;
                g_config.sound.vol_sfx = (uint8_t)v;
            }
        }
        else if (k == KEY_ENTER) {
            if (sel == 1) {
                /* Autodetekcja */
#ifdef DOS_BUILD
                snprintf(status, sizeof(status), "Wykrywam...");
                sound_screen_draw(sel, status);
                SoundCardType det = sound_detect();
                if (det != SND_NONE) {
                    g_config.sound = snd_config;
                    int ci = (int)det;
                    if (ci < 0 || ci >= NUM_CARDS) ci = 0;
                    snprintf(status, sizeof(status), "Wykryto: %s", card_names[ci]);
                } else {
                    snprintf(status, sizeof(status), "Nie wykryto karty!");
                }
#else
                snprintf(status, sizeof(status), "(stub - brak DOS_BUILD)");
#endif
            } else if (sel == 7) {
                /* Test dźwięku SFX — używamy BIOS timer (INT 1Ah) dla opóźnień */
#ifdef DOS_BUILD
                snprintf(status, sizeof(status), "Test SFX...");
                sound_screen_draw(sel, status);
                sound_init(&g_config.sound);
                /* Zagraj trzy SFX z 1-sekundowymi przerwami (18 ticków BIOS ≈ 1s) */
                SfxId sfx_seq[3] = { SFX_CATCH, SFX_MISS, SFX_COMBO };
                for (int si = 0; si < 3; si++) {
                    sound_play_sfx(sfx_seq[si]);
                    __dpmi_regs tr;
                    tr.h.ah = 0; __dpmi_int(0x1A, &tr);
                    uint32_t t0 = ((uint32_t)tr.x.cx << 16) | (uint16_t)tr.x.dx;
                    uint32_t t1 = t0 + 18; /* ~1 sekunda */
                    do {
                        sound_update();
                        for (volatile int d = 0; d < 5000; d++) ;
                        tr.h.ah = 0; __dpmi_int(0x1A, &tr);
                        t0 = ((uint32_t)tr.x.cx << 16) | (uint16_t)tr.x.dx;
                    } while (t0 < t1);
                }
                sound_shutdown();
                snprintf(status, sizeof(status), "SFX OK.");
#else
                snprintf(status, sizeof(status), "SFX test (stub)");
#endif
            } else if (sel == 8) {
                /* Test muzyki — gra do naciśnięcia ESC lub Enter */
#ifdef DOS_BUILD
                snprintf(status, sizeof(status), "Gra muzyka... ESC/Enter=stop");
                sound_screen_draw(sel, status);
                sound_init(&g_config.sound);
                sound_play_music(MUS_TITLE);
                for (;;) {
                    sound_update();
                    for (volatile int d = 0; d < 5000; d++) ;
                    /* Sprawdź klawisz bez blokowania (INT 16h AH=1, ZF=0 → klawisz gotowy) */
                    __dpmi_regs kr;
                    kr.h.ah = 0x01;
                    __dpmi_int(0x16, &kr);
                    if (!(kr.x.flags & 0x40)) {  /* ZF=0 → klawisz w buforze */
                        uint8_t mk = bios_readkey();
                        if (mk == KEY_ESC || mk == KEY_ENTER) break;
                    }
                }
                sound_stop_music();
                sound_shutdown();
                snprintf(status, sizeof(status), "Muzyka zatrzymana.");
#else
                snprintf(status, sizeof(status), "Muzyka test (stub)");
#endif
            }
        }
        else if (k == KEY_F4) {
            /* Reset głośności do domyślnych */
            g_config.sound.vol_music = 80;
            g_config.sound.vol_sfx   = 80;
        }
        else if (k == KEY_ESC || k == KEY_F2) break;
    }
    config_save();
}

/* =========================================================================
   EKRAN 3: TESTY URZĄDZEŃ
   ========================================================================= */
static void screen_tests(void) {
    int sel = 0;

    while (1) {
        bios_clear_screen(ATTR_NORMAL);
        draw_chrome("TESTY");

        draw_box_titled(10, 3, 60, 16, "Testy urzadzen", ATTR_BORDER);

        const char *tests[] = {
            "  Test klawiatury (nacisnij klawisze)     ",
            "  Test joysticka  (porusz drazkiem)       ",
            "  Test myszy serial (ruszyj mysza)        ",
            "  Test dzwiekowy  (odtworz probke SFX)    "
        };
        for (int i = 0; i < 4; i++) {
            put_str(12, (uint8_t)(5+i*3), tests[i],
                    (i == sel) ? ATTR_SELECTED : ATTR_NORMAL);
        }
        put_str(12, 17, "  Enter=start testu    ESC=powrot         ", ATTR_HELP);

        uint8_t k = bios_readkey();
        if      (k == KEY_UP)   sel = (sel > 0) ? sel-1 : 3;
        else if (k == KEY_DOWN) sel = (sel < 3) ? sel+1 : 0;
        else if (k == KEY_ENTER) {
            bios_clear_screen(ATTR_NORMAL);
            fill_row(0, ' ', ATTR_TITLE);
            put_str(2, 0, "TEST — nacisnij ESC aby zakonczyc", ATTR_TITLE);

            if (sel == 0) {
                put_str(2, 3, "Wcisniety klawisz:", ATTR_LABEL);
#ifdef DOS_BUILD
                uint8_t tk;
                do {
                    tk = bios_readkey();
                    char buf[40];
                    snprintf(buf, sizeof(buf),
                             "Scancode: 0x%02X  ASCII: %c     ",
                             tk, (tk >= 0x20 && tk < 0x80) ? tk : '?');
                    put_str(2, 5, buf, ATTR_VALUE);
                } while (tk != KEY_ESC);
#endif
            } else if (sel == 1) {
                put_str(2, 3, "Joystick (Game Port $201):", ATTR_LABEL);
#ifdef DOS_BUILD
                for (int t = 0; t < 300; t++) {
                    uint8_t pb = inportb(0x201);
                    char buf[60];
                    snprintf(buf, sizeof(buf),
                             "Port: 0x%02X  Przyciski: %d %d  Osie: busy=%d",
                             pb, !((pb>>4)&1), !((pb>>5)&1), pb & 0x0F);
                    put_str(2, 5, buf, ATTR_VALUE);
                    for (volatile int d = 0; d < 50000; d++) ;
                }
#endif
            } else if (sel == 2) {
                put_str(2, 3, "Mysz serial (porusz mysza):", ATTR_LABEL);
                put_str(2, 5, "Brak danych — zainicjuj w menu Sterowanie.", ATTR_VALUE);
                bios_readkey();
            } else if (sel == 3) {
                put_str(2, 3, "Test dzwiekowy SFX...", ATTR_LABEL);
#ifdef DOS_BUILD
                sound_init(&g_config.sound);
                SfxId sfx3[3] = { SFX_CATCH, SFX_MISS, SFX_COMBO };
                for (int si = 0; si < 3; si++) {
                    sound_play_sfx(sfx3[si]);
                    __dpmi_regs tr3;
                    tr3.h.ah = 0; __dpmi_int(0x1A, &tr3);
                    uint32_t t0 = ((uint32_t)tr3.x.cx << 16) | (uint16_t)tr3.x.dx;
                    uint32_t t1 = t0 + 18;
                    do {
                        sound_update();
                        for (volatile int d = 0; d < 5000; d++) ;
                        tr3.h.ah = 0; __dpmi_int(0x1A, &tr3);
                        t0 = ((uint32_t)tr3.x.cx << 16) | (uint16_t)tr3.x.dx;
                    } while (t0 < t1);
                }
                sound_test();
                sound_shutdown();
#endif
                put_str(2, 5, "Gotowe. Nacisnij klawisz.", ATTR_VALUE);
                bios_readkey();
            }
        }
        else if (k == KEY_ESC || k == KEY_F3) break;
    }
}

/* =========================================================================
   EKRAN GŁÓWNY
   ========================================================================= */
static void screen_main(void) {
    bios_clear_screen(ATTR_NORMAL);

    /* Nagłówek */
    fill_row(0, ' ', ATTR_TITLE);
    put_str_centered(0, 0, SCR_COLS, "GRUNIOZERCA DOS v1.0 — SETUP", ATTR_TITLE);

    /* Główne menu */
    draw_box_titled(15, 3, 50, 17, "Main Menu", ATTR_BORDER);
    put_str_centered(15, 2, 50, "Main Menu", ATTR_LABEL); /* tytuł w ramce */

    const char *items[] = {
        "  F1  Sterowanie  (klawiatura/joystick/mysz)",
        "  F2  Dzwiek      (SB / OPL2 / PC Speaker) ",
        "  F3  Testy       (klawiatura/joy/mysz/sfx) ",
        "  F4  Domyslne    (przywroc ustawienia)     ",
        "  ESC Wyjscie                               "
    };
    for (int i = 0; i < 5; i++)
        put_str(17, (uint8_t)(5 + i*2), items[i],
                (i < 4) ? ATTR_NORMAL : ATTR_LABEL);

    /* Prawy panel z info */
    draw_box_titled(0, 3, 14, 17, "Skroty", ATTR_BORDER);
    put_str(2,  5, "F1", ATTR_KEY);
    put_str(2,  7, "F2", ATTR_KEY);
    put_str(2,  9, "F3", ATTR_KEY);
    put_str(2, 11, "F4", ATTR_KEY);
    put_str(1, 13, "ESC", ATTR_KEY);
    put_str(2, 15, "S", ATTR_DIM);
    put_str(2, 16, "t", ATTR_DIM);
    put_str(2, 17, "r", ATTR_DIM);
    put_str(2, 18, "z", ATTR_DIM);

    /* Stopka */
    fill_row(SCR_ROWS-1, ' ', ATTR_TITLE);
    put_str(2, SCR_ROWS-1,
            "Konfiguracja zapisywana do GRUNIO.CFG — Nacisnij F1-F4 lub ESC",
            ATTR_TITLE);
}

/* =========================================================================
   main SETUP
   ========================================================================= */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    config_load();
    bios_cursor_hide();
    screen_main();

    int running = 1;
    while (running) {
        uint8_t k = bios_readkey();
        switch (k) {
        case KEY_F1: screen_input(); screen_main(); break;
        case KEY_F2: screen_sound(); screen_main(); break;
        case KEY_F3: screen_tests(); screen_main(); break;
        case KEY_F4:
            config_defaults();
            config_save();
            fill_row(SCR_ROWS-2, ' ', ATTR_SELECTED);
            put_str(15, SCR_ROWS-2,
                    " Przywrocono ustawienia domyslne. Nacisnij klawisz. ",
                    ATTR_SELECTED);
            bios_readkey();
            screen_main();
            break;
        case KEY_ESC:
            running = 0;
            break;
        default:
            break;
        }
    }

    config_save();
    bios_cursor_show();
    bios_clear_screen(0x07);
    bios_gotoxy(0, 0);
    printf("Konfiguracja zapisana do %s\nMozesz teraz uruchomic GRUNIO.EXE\n",
           CONFIG_FILE);
    return 0;
}
