/*
 * video.h — Interfejs modułu VGA (tryb 13h, 320×200, 256 kolorów)
 * Gruniożerca DOS port
 *
 * Architektura:
 *  - back buffer w RAM (320×200 bytes) → flip do 0xA0000 podczas VBlank
 *  - Tilemap 40×25 kafelków 8×8 px (tło)
 *  - Sprite blitting z maską przezroczystości (kolor 0 = transparent)
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include "dos_compat.h"

/* ---------- Stałe tilemap ------------------------------------------------- */
#define TILE_W       8
#define TILE_H       8
#define TILEMAP_COLS (SCREEN_W / TILE_W)   /* 40 */
#define TILEMAP_ROWS (SCREEN_H / TILE_H)   /* 25 */
#define TILE_COUNT   512   /* dwie strony CHR-ROM: tło [0..255] + sprite [256..511] */

/* Marginesy — gra NES jest 256px szersza; centrujemy w 320px */
#define PLAY_X_OFFSET  32   /* piksele od lewej krawędzi do obszaru gry */
#define PLAY_W         256  /* szerokość obszaru gry */
#define PLAY_H         168  /* wysokość obszaru gry */
#define UI_Y           168  /* wiersz startowy paska UI — zostawia 32px marginesu CRT */

/* ---------- Struktury ----------------------------------------------------- */

/* Pojedynczy kafelek w tilemap (indeks do sprites.dat + paleta) */
typedef struct {
    uint8_t tile_id;   /* 0..255 — indeks w sprites.dat */
    uint8_t palette;   /* 0..3   — paleta (odpowiada NES bg palette 0..3) */
} Tile;

/* Definicja palety VGA: 256 kolorów × 3 bajty (R,G,B, 0-63 zakres VGA) */
typedef uint8_t VGAPalette[256][3];

/* ---------- API ----------------------------------------------------------- */

/* Inicjalizacja: przejście do Mode 13h, ładuje paletę, zerowuje bufory.
   Ścieżka do palette.dat (wygenerowanej przez chr2vga). */
void video_init(const char *palette_path);

/* Powrót do trybu tekstowego (Mode 3) — wywołaj przed wyjściem z gry. */
void video_shutdown(void);

/* Ładuje kafelki ze sprites.dat do pamięci wewnętrznej (256 × 64 bajtów). */
void video_load_sprites(const char *sprites_path);

/* Czeka na VBlank (port 0x3DA bit 3) i kopiuje back buffer → VRAM. */
void video_flip(void);

/* Czyści back buffer (wypełnia kolorem 0). */
void video_clear(void);

/* Rysuje kafelek tile_id na back buffer (x,y — lewy górny róg, piksele). */
void video_draw_tile(int x, int y, uint8_t tile_id, uint8_t palette);

/* Rysuje tilemap pełnoekranową (40×25 kafelków). */
void video_draw_tilemap(const Tile tilemap[TILEMAP_ROWS][TILEMAP_COLS]);

/* Blituje sprite 8×8 z maską — kolor 0 = transparent.
   x,y — pozycja na back buffer; tile_id — indeks w sprites.dat;
   palette — paleta 0..3; flip_h — lustro poziome. */
void video_draw_sprite(int x, int y, uint8_t tile_id, uint8_t palette, int flip_h);

/* Rysuje meta-sprite złożony z kilku kafelków 8×8.
   tiles[] = { tile_id, dx, dy, palette, flip_h, ... }, zakończony 0xFF. */
void video_draw_meta_sprite(int x, int y, const uint8_t *tiles);

/* Rysuje prostokąt (wypełniony) na back buffer. */
void video_fill_rect(int x, int y, int w, int h, uint8_t color);

/* Rysuje piksel bezpośrednio na back buffer. */
static inline void video_put_pixel(int x, int y, uint8_t color);

/* Ładuje nową paletę VGA ze struktury (64-wartości RGB). */
void video_set_palette(const VGAPalette pal);

/* Przywraca oryginalną paletę załadowaną z palette.dat (po nadpisaniu przez PCX). */
void video_restore_game_palette(void);

/* Przywraca sloty [from, to) z game_pal — użyteczne do selektywnego zachowania
   kolorów sprite'ów NES przy aktywnej palecie PCX (np. pig na tle hiscore.pcx). */
void video_restore_game_palette_range(int from, int to);

/* Nakłada surową paletę PCX (768 bajtów, RGB 0–255) na indeksy [from..255].
   Indeksy 0..(from-1) zostają nienaruszone — używaj from=64 by zachować sprite'y. */
void video_overlay_raw_palette(const uint8_t *raw, int from);

/* Ustawia pojedynczy kolor w palecie (wartości 0–63 VGA DAC).
   Aktualizuje też kopię fade_pal — efekt fade działa poprawnie. */
void video_set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/* Efekt fade — stopniowo przyciemnia/rozjaśnia paletę.
   step: +1 = rozjaśniaj, -1 = przyciemniaj. Zwraca 0 gdy gotowe. */
int video_fade_step(int direction);

/* Resetuje poziom fade do 0 (pełna czerń) — użyj przed fade-in. */
void video_fade_reset(void);

/* Rysuje znak ASCII z wbudowanego fonta 8×8 na back buffer. */
void video_draw_char(int x, int y, char c, uint8_t fg_color);

/* Rysuje napis (terminator '\0'). */
void video_draw_string(int x, int y, const char *str, uint8_t fg_color);

/* Zwraca wskaźnik do back buffera (tylko do odczytu przez inne moduły). */
const uint8_t *video_get_backbuf(void);

/* Wewnętrzne — inline dla szybkości */
extern uint8_t video_backbuf[SCREEN_H * SCREEN_W];

static inline void video_put_pixel(int x, int y, uint8_t color) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        video_backbuf[y * SCREEN_W + x] = color;
}

#endif /* VIDEO_H */
