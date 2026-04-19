/*
 * video.c — VGA Mode 13h (320×200, 256 kolorów), back buffer, sprite blitting
 * Gruniożerca DOS port, 2024
 *
 * Architektura renderingu:
 *   1. Czyść back buffer (video_clear)
 *   2. Rysuj tło (video_draw_tilemap)
 *   3. Rysuj sprite'y (video_draw_sprite / video_draw_meta_sprite)
 *   4. Kopiuj back buffer → VRAM podczas VBlank (video_flip)
 *
 * sprites.dat: 256 kafelków × 64 bajty (8×8 pikseli, 1 bajt/piksel, indeks palety)
 * palette.dat: 256 kolorów × 3 bajty (R, G, B w zakresie 0–63 dla VGA DAC)
 */
#include "video.h"
#include "dos_compat.h"
#include "pack.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---------- Dane wewnętrzne ------------------------------------------------ */

/* Back buffer — 320×200 = 64 000 bajtów */
uint8_t video_backbuf[SCREEN_H * SCREEN_W];

/* Cache kafelków — 256 sprite'ów × 64 bajty */
static uint8_t tile_cache[TILE_COUNT][TILE_H * TILE_W];

/* Cache wersji odbitej poziomo każdego kafelka */
static uint8_t tile_cache_flipped[TILE_COUNT][TILE_H * TILE_W];

/* Aktualna paleta (wartości zdalne VGA DAC: 0–63) */
static VGAPalette current_pal;
static VGAPalette fade_pal;    /* kopia do efektu fade */
static VGAPalette game_pal;    /* oryginalna paleta z palette.dat — do przywrócenia po PCX */
static int        fade_level;  /* 0 = czarny, 63 = pełna jasność */

#ifdef DOS_BUILD
/* Wskaźnik do VRAM przez near pointer DJGPP */
static uint8_t *vga_mem = NULL;
#endif

/* Wbudowany font 8×8 (IBM CP437, tylko ASCII 0x20–0x7F).
   Każdy znak: 8 bajtów = 8 wierszy × 8 bitów (bit7=lewy piksel). */
#include "font8x8.h"  /* plik generowany przez chr2vga lub dostarczony osobno */

/* ---------- Inicjalizacja -------------------------------------------------- */

#ifdef DOS_BUILD
static void set_mode13h(void) {
    __dpmi_regs r;
    r.x.ax = 0x0013;   /* AH=0: set mode, AL=0x13: Mode 13h */
    __dpmi_int(0x10, &r);
}

static void set_mode3h(void) {
    __dpmi_regs r;
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
}

static void vga_set_palette_entry(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    /* VGA DAC: wartości 0–63 */
    outportb(0x3C8, idx);
    outportb(0x3C9, r);
    outportb(0x3C9, g);
    outportb(0x3C9, b);
}

static void vga_upload_palette(const VGAPalette pal) {
    outportb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outportb(0x3C9, pal[i][0]);
        outportb(0x3C9, pal[i][1]);
        outportb(0x3C9, pal[i][2]);
    }
}
#endif

void video_init(const char *palette_path) {
#ifdef DOS_BUILD
    set_mode13h();

    /* Włącz near pointer access (dostęp do 0xA0000) */
    if (__djgpp_nearptr_enable() == 0) {
        fprintf(stderr, "Błąd: nie można włączyć near pointer!\n");
        exit(1);
    }
    vga_mem = (uint8_t *)(0xA0000 + __djgpp_conventional_base);
#endif

    memset(video_backbuf, 0, sizeof(video_backbuf));
    memset(tile_cache, 0, sizeof(tile_cache));
    fade_level = 63;

    /* Ładuj paletę — najpierw z archiwum GRUNIO.DAT, potem z pliku */
    {
        uint32_t psz = 0;
        const uint8_t *pdata = palette_path
            ? pack_get_path(palette_path, &psz) : NULL;
        if (pdata && psz == sizeof(current_pal)) {
            memcpy(current_pal, pdata, sizeof(current_pal));
        } else if (palette_path) {
            FILE *f = fopen(palette_path, "rb");
            if (f) { fread(current_pal, 1, sizeof(current_pal), f); fclose(f); }
        }
    }

    /* Ustaw kolor 0 = czarny (transparent) */
    current_pal[0][0] = 0;
    current_pal[0][1] = 0;
    current_pal[0][2] = 0;

    /* Skopiuj do fade_pal i game_pal */
    memcpy(fade_pal, current_pal, sizeof(fade_pal));
    memcpy(game_pal, current_pal, sizeof(game_pal));

#ifdef DOS_BUILD
    vga_upload_palette(current_pal);
#endif
}

void video_shutdown(void) {
#ifdef DOS_BUILD
    __djgpp_nearptr_disable();
    set_mode3h();
#endif
}

/* ---------- Ładowanie sprite'ów ------------------------------------------- */

void video_load_sprites(const char *sprites_path) {
    /* Najpierw spróbuj z archiwum GRUNIO.DAT */
    uint32_t ssz = 0;
    const uint8_t *sdata = sprites_path
        ? pack_get_path(sprites_path, &ssz) : NULL;
    if (sdata && ssz > 0) {
        size_t copy = ssz < sizeof(tile_cache) ? ssz : sizeof(tile_cache);
        memcpy(tile_cache, sdata, copy);
    } else {
        FILE *f = sprites_path ? fopen(sprites_path, "rb") : NULL;
        if (!f) {
            fprintf(stderr, "Ostrzeżenie: nie znaleziono %s\n",
                    sprites_path ? sprites_path : "(null)");
            for (int t = 0; t < TILE_COUNT; t++)
                for (int p = 0; p < TILE_H * TILE_W; p++)
                    tile_cache[t][p] = (p + t) & 0xFF;
        } else {
            fread(tile_cache, 1, sizeof(tile_cache), f);
            fclose(f);
        }
    }

    /* Pre-generuj wersje odwrócone poziomo */
    for (int t = 0; t < TILE_COUNT; t++) {
        for (int row = 0; row < TILE_H; row++) {
            for (int col = 0; col < TILE_W; col++) {
                tile_cache_flipped[t][row * TILE_W + col] =
                    tile_cache[t][row * TILE_W + (TILE_W - 1 - col)];
            }
        }
    }
}

/* ---------- Flip (back buffer → VRAM) -------------------------------------- */

void video_flip(void) {
#ifdef DOS_BUILD
    /* Czekaj na VBlank: port 0x3DA */
    /* Czekaj na zakończenie VBlank (bit 3 = 0) */
    while (inportb(0x3DA) & 0x08) ;
    /* Czekaj na początek VBlank (bit 3 = 1) */
    while (!(inportb(0x3DA) & 0x08)) ;

    /* Skopiuj 64 000 bajtów do VRAM */
    memcpy(vga_mem, video_backbuf, SCREEN_W * SCREEN_H);
#else
    /* host: brak operacji */
    (void)vga_mem;
#endif
}

void video_clear(void) {
    memset(video_backbuf, 0, SCREEN_W * SCREEN_H);
}

/* ---------- Rysowanie kafelków -------------------------------------------- */

void video_draw_tile(int x, int y, uint8_t tile_id, uint8_t palette) {
    /* palette 0..3 — przesuwa indeksy kolorów sprite'a o palette*4 */
    const uint8_t *src = tile_cache[tile_id];
    const int      pal_offset = (int)palette * 4;
    int            sx, sy;

    for (sy = 0; sy < TILE_H; sy++) {
        int py = y + sy;
        if ((unsigned)py >= SCREEN_H) continue;
        uint8_t *dst_row = &video_backbuf[py * SCREEN_W + x];
        const uint8_t *src_row = src + sy * TILE_W;
        for (sx = 0; sx < TILE_W; sx++) {
            int px = x + sx;
            if ((unsigned)px >= SCREEN_W) continue;
            uint8_t c = src_row[sx];
            if (c != 0)  /* kolor 0 = transparent */
                dst_row[sx] = (uint8_t)(c + pal_offset);
        }
    }
}

void video_draw_tilemap(const Tile tilemap[TILEMAP_ROWS][TILEMAP_COLS]) {
    for (int row = 0; row < TILEMAP_ROWS; row++) {
        for (int col = 0; col < TILEMAP_COLS; col++) {
            const Tile *t = &tilemap[row][col];
            if (t->tile_id == 0) continue;
            video_draw_tile(col * TILE_W, row * TILE_H, t->tile_id, t->palette);
        }
    }
}

/* ---------- Rysowanie sprite'ów ------------------------------------------ */

void video_draw_sprite(int x, int y, uint8_t tile_id, uint8_t palette, int flip_h) {
    const uint8_t *src = flip_h ? tile_cache_flipped[tile_id] : tile_cache[tile_id];
    const int      pal_offset = (int)palette * 4;

    for (int sy = 0; sy < TILE_H; sy++) {
        int py = y + sy;
        if ((unsigned)py >= SCREEN_H) continue;
        const uint8_t *src_row = src + sy * TILE_W;
        for (int sx = 0; sx < TILE_W; sx++) {
            int px = x + sx;
            if ((unsigned)px >= SCREEN_W) continue;
            uint8_t c = src_row[sx];
            if (c != 0)
                video_backbuf[py * SCREEN_W + px] = (uint8_t)(c + pal_offset);
        }
    }
}

/* Format meta-sprite (odpowiednik nes/Sys/Sprity.asm):
   { tile_id, dx, dy, palette, flip_h, ... , 0xFF (koniec) }
   Każdy wpis = 5 bajtów, koniec = 0xFF jako pierwszy bajt. */
void video_draw_meta_sprite(int x, int y, const uint8_t *tiles) {
    while (*tiles != 0xFF) {
        uint8_t tile_id = tiles[0];
        int     dx      = (int8_t)tiles[1];
        int     dy      = (int8_t)tiles[2];
        uint8_t palette = tiles[3];
        int     flip_h  = tiles[4];
        video_draw_sprite(x + dx, y + dy, tile_id, palette, flip_h);
        tiles += 5;
    }
}

/* ---------- Narzędzia rysowania ------------------------------------------- */

void video_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if ((unsigned)py >= SCREEN_H) continue;
        int x0 = x < 0 ? 0 : x;
        int x1 = x + w; if (x1 > (int)SCREEN_W) x1 = SCREEN_W;
        if (x0 >= x1) continue;
        memset(&video_backbuf[py * SCREEN_W + x0], color, (size_t)(x1 - x0));
    }
}

void video_set_palette(const VGAPalette pal) {
    memcpy(current_pal, pal, sizeof(current_pal));
    memcpy(fade_pal, pal, sizeof(fade_pal));
#ifdef DOS_BUILD
    vga_upload_palette(current_pal);
#endif
}

void video_set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    current_pal[idx][0] = r;
    current_pal[idx][1] = g;
    current_pal[idx][2] = b;
    fade_pal[idx][0] = r;
    fade_pal[idx][1] = g;
    fade_pal[idx][2] = b;
#ifdef DOS_BUILD
    vga_set_palette_entry(idx, r, g, b);
#endif
}

/* Nakłada surową paletę PCX (RGB 0–255) na indeksy [from, 255].
 * Indeksy 0..(from-1) zostają nienaruszone (sprite'y NES).
 * raw: tablica 768 bajtów (256×R,G,B) z bloku palety PCX.
 * Wartości 8-bit → dzielone przez 4 → VGA DAC 0–63. */
void video_overlay_raw_palette(const uint8_t *raw, int from) {
    for (int i = from; i < 256; i++) {
        uint8_t r = raw[i * 3 + 0] >> 2;
        uint8_t g = raw[i * 3 + 1] >> 2;
        uint8_t b = raw[i * 3 + 2] >> 2;
        current_pal[i][0] = r;
        current_pal[i][1] = g;
        current_pal[i][2] = b;
        fade_pal[i][0]    = r;
        fade_pal[i][1]    = g;
        fade_pal[i][2]    = b;
    }
#ifdef DOS_BUILD
    vga_upload_palette(current_pal);
#endif
}

/* Przywraca oryginalną paletę z palette.dat (po nadpisaniu przez PCX tytułu) */
void video_restore_game_palette(void) {
    memcpy(current_pal, game_pal, sizeof(current_pal));
    memcpy(fade_pal,    game_pal, sizeof(fade_pal));
#ifdef DOS_BUILD
    vga_upload_palette(current_pal);
#endif
}

/* Przywraca tylko sloty [from, to) z game_pal.
 * Używane gdy aktywna jest paleta PCX ale potrzebujemy kolorów NES dla sprite'ów.
 * Np. video_restore_game_palette_range(0, 8) przywraca sub-palety 0-1 dla świnki. */
void video_restore_game_palette_range(int from, int to) {
    int i;
    if (to > 256) to = 256;
    for (i = from; i < to; i++) {
        current_pal[i][0] = game_pal[i][0];
        current_pal[i][1] = game_pal[i][1];
        current_pal[i][2] = game_pal[i][2];
        fade_pal[i][0]    = game_pal[i][0];
        fade_pal[i][1]    = game_pal[i][1];
        fade_pal[i][2]    = game_pal[i][2];
    }
#ifdef DOS_BUILD
    vga_upload_palette(current_pal);
#endif
}

/* ---------- Fade efekt ---------------------------------------------------- */

void video_fade_reset(void) {
    fade_level = 0;
#ifdef DOS_BUILD
    /* Natychmiast ustaw paletę na czarną */
    outportb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outportb(0x3C9, 0);
        outportb(0x3C9, 0);
        outportb(0x3C9, 0);
    }
#endif
}

int video_fade_step(int direction) {
    /* direction: +1 = fade in (rozjaśniaj), -1 = fade out (przyciemniaj) */
    fade_level += direction * 2;
    if (fade_level > 63) fade_level = 63;
    if (fade_level < 0)  fade_level = 0;

#ifdef DOS_BUILD
    outportb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outportb(0x3C9, (uint8_t)((int)fade_pal[i][0] * fade_level / 63));
        outportb(0x3C9, (uint8_t)((int)fade_pal[i][1] * fade_level / 63));
        outportb(0x3C9, (uint8_t)((int)fade_pal[i][2] * fade_level / 63));
    }
#endif

    if (direction > 0) return (fade_level >= 63) ? 0 : 1;
    else               return (fade_level <= 0)  ? 0 : 1;
}

/* ---------- Tekst ---------------------------------------------------------- */

/* Mapowanie ASCII → tile_id z NES CHR ROM (sprites.dat).
 * Źródło: Credits.asm → "STRING"-54 dla liter, wizualna analiza CHR dla cyfr.
 *   ' '      → 0   (przezroczysty — nie rysuj)
 *   '0'-'9'  → 1–10 (chr - 47)
 *   'A'-'Z'  → 11–36 (chr - 54)
 *   'a'-'z'  → 11–36 (chr - 86, te same kafelki co wielkie litery)
 *   '.'      → 43 ($2B)
 *   ':'      → 44 ($2C)
 *   inne     → 0xFF (fallback: bitmap font IBM CP437)
 */
static uint8_t nes_char_tile(unsigned char c) {
    if (c == ' ')              return 0;
    if (c >= '0' && c <= '9') return (uint8_t)(c - 47);
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 54);
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 86);
    if (c == '.')              return 43;
    if (c == ':')              return 44;
    return 0xFF;  /* nie obsługiwany — użyj bitmap fallback */
}

void video_draw_char(int x, int y, char c, uint8_t fg_color) {
    uint8_t tile = nes_char_tile((unsigned char)c);

    if (tile == 0) return;  /* spacja = puste miejsce */

    if (tile != 0xFF) {
        /* Czcionka NES — piksele mają wartość 0 (tło) lub 3 (znak) */
        const uint8_t *src = tile_cache[tile];
        for (int sy = 0; sy < TILE_H; sy++) {
            int py = y + sy;
            if ((unsigned)py >= SCREEN_H) continue;
            for (int sx = 0; sx < TILE_W; sx++) {
                int px = x + sx;
                if ((unsigned)px >= SCREEN_W) continue;
                if (src[sy * TILE_W + sx] != 0)
                    video_backbuf[py * SCREEN_W + px] = fg_color;
            }
        }
        return;
    }

    /* Fallback: bitmap font IBM CP437 dla znaków spoza NES CHR */
    if (c < 0x20 || c > 0x7E) return;
#ifdef FONT8X8_H
    const uint8_t *glyph = font8x8_basic[(uint8_t)c - 0x20];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col))
                video_put_pixel(x + col, y + row, fg_color);
        }
    }
#else
    video_fill_rect(x, y, 6, 8, fg_color);
#endif
}

void video_draw_string(int x, int y, const char *str, uint8_t fg_color) {
    while (*str) {
        video_draw_char(x, y, *str++, fg_color);
        x += TILE_W;
    }
}

const uint8_t *video_get_backbuf(void) {
    return video_backbuf;
}
