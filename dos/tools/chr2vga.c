/*
 * chr2vga.c — Konwerter CHR-ROM NES (2bpp) → format VGA sprites (8bpp)
 * Gruniożerca DOS port, 2024
 * Narzędzie hosta — kompilowane zwykłym gcc, NIE przez DJGPP.
 *
 * Użycie:
 *   chr2vga <input.chr> <output_sprites.dat> <output_palette.dat>
 *
 * CHR-ROM format NES (2bpp):
 *   Każdy sprite 8×8 = 16 bajtów:
 *     Bajty 0-7:  bitplane 0 (LSB koloru) — wiersz 0..7
 *     Bajty 8-15: bitplane 1 (MSB koloru) — wiersz 0..7
 *   Piksel (x,y): bit = row_low[y] >> (7-x) & 1 | (row_high[y] >> (7-x) & 1) << 1
 *   Wynik: kolor indeks 0..3 (0=transparent, 1-3=kolor palety)
 *
 * Wyjście sprites.dat:
 *   256 sprite'ów × 64 bajty (8×8 pikseli, 1 bajt/piksel = indeks 0..3)
 *   Kolor 0 = transparent (konwencja DOS portu).
 *
 * Wyjście palette.dat:
 *   256 kolorów × 3 bajty (R, G, B w zakresie 0–63 dla VGA DAC).
 *   Mapowanie: 4 palety NES × 4 kolory → VGA (zakres dostosowany).
 *
 * NES palety sprzętowe (52 kolory) konwertowane do VGA RGB (0-63):
 *   Używamy standardowych wartości RGB palety NES NTSC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
   Paleta NES NTSC — 64 kolory, wartości RGB (0–255), indeksy 0x00..0x3F
   Źródło: powszechnie znana tabela referencyjna NES NTSC palette
   ========================================================================= */
static const uint8_t nes_palette_rgb[64][3] = {
    {84, 84, 84}, {0, 30, 116}, {8, 16, 144}, {48, 0, 136},     /* 0x00-0x03 */
    {68, 0, 100}, {92, 0, 48},  {84, 4, 0},   {60, 24, 0},      /* 0x04-0x07 */
    {32, 42, 0},  {8, 58, 0},   {0, 64, 0},   {0, 60, 0},       /* 0x08-0x0B */
    {0, 50, 60},  {0, 0, 0},    {0, 0, 0},    {0, 0, 0},        /* 0x0C-0x0F */
    {152,150,152},{8, 76, 196}, {48, 50, 236},{92, 30, 228},     /* 0x10-0x13 */
    {136,20, 176},{160,20, 100},{152,34, 32}, {120,60, 0},       /* 0x14-0x17 */
    {84, 90, 0},  {40, 114, 0}, {8, 124, 0},  {0, 118, 40},     /* 0x18-0x1B */
    {0, 102, 120},{0, 0, 0},    {0, 0, 0},    {0, 0, 0},        /* 0x1C-0x1F */
    {236,238,236},{76, 154, 236},{120,124,236},{176,98, 236},    /* 0x20-0x23 */
    {228,84, 236},{236,88, 180},{236,106,100},{212,136,32},      /* 0x24-0x27 */
    {160,170,0},  {116,196,0},  {76, 208,32}, {56, 204,108},    /* 0x28-0x2B */
    {56, 180,204},{60, 60, 60}, {0, 0, 0},    {0, 0, 0},        /* 0x2C-0x2F */
    {236,238,236},{168,204,236},{188,188,236},{212,178,236},     /* 0x30-0x33 */
    {236,174,236},{236,174,212},{236,180,176},{228,196,144},     /* 0x34-0x37 */
    {204,210,120},{180,222,120},{168,226,144},{152,226,180},     /* 0x38-0x3B */
    {160,214,228},{160,162,160},{0, 0, 0},    {0, 0, 0},        /* 0x3C-0x3F */
};

/* Domyślne palety gry Gruniożerca NES:
   Paleta tła 0: [0x0F(czarny), 0x16(czerwony), 0x27(żółty), 0x30(biały)]
   Paleta sprite 0: [-, 0x16, 0x27, 0x30] (kolor 0=transparent)
   Paleta sprite 1: [-, 0x11, 0x21, 0x31] (niebieski)
   Paleta sprite 2: [-, 0x19, 0x29, 0x39] (zielony)
   Paleta sprite 3: [-, 0x14, 0x24, 0x34] (fioletowy)
   (odpowiada pigcolor 0..3 w grze NES) */
static const uint8_t game_palettes[4][4] = {
    {0x0F, 0x16, 0x27, 0x30},  /* czerwony/żółty */
    {0x0F, 0x11, 0x21, 0x31},  /* niebieski */
    {0x0F, 0x19, 0x29, 0x39},  /* zielony */
    {0x0F, 0x14, 0x24, 0x34},  /* fioletowy */
};

/* =========================================================================
   Konwersja kolorów NES (0-255 RGB) → VGA (0-63)
   ========================================================================= */
static uint8_t rgb_to_vga(uint8_t val) {
    return (uint8_t)(((uint32_t)val * 63 + 127) / 255);
}

/* =========================================================================
   Generuj paletę VGA 256 kolorów
   Układ:
     [0]    = kolor 0 (czarny / transparent)
     [1-3]  = paleta 0, kolory 1-3
     [4]    = kolor 0 (wspólny)
     [5-7]  = paleta 1, kolory 1-3
     [8-11] = paleta 2, kolory 1-3
     ...
     [16-255] = wypełnienie (dodatkowe kolory, font itd.)
   ========================================================================= */
static void build_vga_palette(uint8_t pal_out[256][3]) {
    memset(pal_out, 0, 256 * 3);

    /* Kolor 0 = czarny (transparent) */
    pal_out[0][0] = pal_out[0][1] = pal_out[0][2] = 0;

    for (int p = 0; p < 4; p++) {
        for (int c = 0; c < 4; c++) {
            int vga_idx = p * 4 + c;
            if (vga_idx == 0) continue; /* zachowaj czarny */
            uint8_t nes_col = game_palettes[p][c];
            if (nes_col >= 64) nes_col = 0x0F;
            pal_out[vga_idx][0] = rgb_to_vga(nes_palette_rgb[nes_col][0]);
            pal_out[vga_idx][1] = rgb_to_vga(nes_palette_rgb[nes_col][1]);
            pal_out[vga_idx][2] = rgb_to_vga(nes_palette_rgb[nes_col][2]);
        }
    }

    /* Dodatkowe kolory dla tekstu/UI:
       indeks 16: szary (tekst tła)
       indeks 17: biały jasny (tekst UI)
       indeks 18-19: inne */
    pal_out[16][0] = 42; pal_out[16][1] = 42; pal_out[16][2] = 42;
    pal_out[17][0] = 63; pal_out[17][1] = 63; pal_out[17][2] = 63;
    pal_out[18][0] = 63; pal_out[18][1] = 63; pal_out[18][2] = 0;  /* żółty */
    pal_out[19][0] = 0;  pal_out[19][1] = 63; pal_out[19][2] = 0;  /* zielony */

    /* Cyfry 0-9 (używane przez score.c) — kolor biały na czarnym */
    for (int i = 20; i < 256; i++) {
        pal_out[i][0] = 40; pal_out[i][1] = 40; pal_out[i][2] = 40;
    }
}

/* =========================================================================
   Konwersja CHR-ROM tile 8×8 (16 bajtów NES 2bpp) → 64 bajty VGA
   ========================================================================= */
static void convert_tile(const uint8_t *chr_tile, uint8_t *out_tile) {
    for (int row = 0; row < 8; row++) {
        uint8_t plane0 = chr_tile[row];        /* LSB */
        uint8_t plane1 = chr_tile[row + 8];    /* MSB */
        for (int col = 0; col < 8; col++) {
            int bit = 7 - col;
            uint8_t lo = (plane0 >> bit) & 1;
            uint8_t hi = (plane1 >> bit) & 1;
            uint8_t color_idx = (uint8_t)((hi << 1) | lo);  /* 0..3 */
            out_tile[row * 8 + col] = color_idx;
        }
    }
}

/* =========================================================================
   main
   ========================================================================= */
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Użycie: chr2vga <input.chr> <sprites.dat> <palette.dat>\n");
        fprintf(stderr, "Przykład: chr2vga ../nes/Gfx/chr.chr assets/sprites.dat assets/palette.dat\n");
        return 1;
    }

    const char *chr_path     = argv[1];
    const char *sprites_path = argv[2];
    const char *palette_path = argv[3];

    /* Wczytaj CHR-ROM */
    FILE *fin = fopen(chr_path, "rb");
    if (!fin) {
        fprintf(stderr, "Błąd: nie można otworzyć %s\n", chr_path);
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long chr_size = ftell(fin);
    rewind(fin);

    uint8_t *chr_data = (uint8_t *)malloc((size_t)chr_size);
    if (!chr_data) { fclose(fin); fprintf(stderr, "Brak pamięci\n"); return 1; }
    fread(chr_data, 1, (size_t)chr_size, fin);
    fclose(fin);

    int tile_count = (int)(chr_size / 16);
    if (tile_count > 512) tile_count = 512;
    printf("CHR-ROM: %ld bajtów, %d kafelków 8×8\n", chr_size, tile_count);

    /* Konwertuj kafelki — obie strony CHR-ROM (tło + sprite) */
    uint8_t sprites[512][64];
    memset(sprites, 0, sizeof(sprites));
    for (int i = 0; i < tile_count; i++) {
        convert_tile(chr_data + i * 16, sprites[i]);
    }
    free(chr_data);

    /* Zapisz sprites.dat */
    FILE *fout = fopen(sprites_path, "wb");
    if (!fout) { fprintf(stderr, "Błąd: nie można zapisać %s\n", sprites_path); return 1; }
    fwrite(sprites, 1, (size_t)tile_count * 64, fout);
    fclose(fout);
    printf("Zapisano: %s (%d kafelków × 64B = %d B)\n",
           sprites_path, tile_count, tile_count * 64);

    /* Generuj i zapisz paletę VGA */
    uint8_t vga_pal[256][3];
    build_vga_palette(vga_pal);

    fout = fopen(palette_path, "wb");
    if (!fout) { fprintf(stderr, "Błąd: nie można zapisać %s\n", palette_path); return 1; }
    fwrite(vga_pal, 1, sizeof(vga_pal), fout);
    fclose(fout);
    printf("Zapisano: %s (256 kolorów × 3B RGB VGA)\n", palette_path);

    printf("Gotowe! Wygenerowano assets dla Gruniożerca DOS.\n");
    return 0;
}
