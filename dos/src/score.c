/*
 * score.c — Wynik i high score — port z nes/Sys/hscore.asm
 * Gruniożerca DOS port, 2024
 */
#include "score.h"
#include "video.h"
#include <stdio.h>
#include <string.h>

/* tile_id cyfr 0-9 z NES CHR-ROM.
 * Schemat NES: score bajt inicjowany 0x10, wyświetlany jako tile = score-0x0F.
 * score 0x10 → tile 1 = '0',  score 0x18 → tile 9 = '9'. */
#define DIGIT_TILE_BASE  1    /* tile 1='0', tile 2='1', ..., tile 10='9' */

static uint32_t current_score = 0;
static uint32_t hi_score      = 0;

void score_init(void) {
    current_score = 0;
    /* Załaduj high score z pliku */
    FILE *f = fopen(HISCORE_FILE, "rb");
    if (f) {
        fread(&hi_score, sizeof(hi_score), 1, f);
        fclose(f);
    } else {
        hi_score = 0;
    }
}

void score_add(uint32_t points) {
    current_score += points;
    if (current_score > 9999999) current_score = 9999999;
}

uint32_t score_get(void)    { return current_score; }
uint32_t score_get_hi(void) { return hi_score; }

int score_check_hi(void) {
    if (current_score > hi_score) {
        hi_score = current_score;
        score_save_hi();
        return 1;
    }
    return 0;
}

void score_save_hi(void) {
    FILE *f = fopen(HISCORE_FILE, "wb");
    if (f) {
        fwrite(&hi_score, sizeof(hi_score), 1, f);
        fclose(f);
    }
}

void score_reset(void) {
    current_score = 0;
}

/* Rysuje 6-cyfrowy wynik jako kafelki 8×8 */
void score_draw(int x, int y, uint32_t val) {
    char buf[7];
    snprintf(buf, sizeof(buf), "%06u", (unsigned)(val % 1000000));
    for (int i = 0; i < 6; i++) {
        uint8_t digit = (uint8_t)(buf[i] - '0');
        video_draw_sprite(x + i * TILE_W, y, DIGIT_TILE_BASE + digit, 0, 0);
    }
}

#define UI_TEXT_Y  (UI_Y + 12)   /* wyśrodkowanie w pasku UI 32 px: (32-8)/2 = 12 */

void score_draw_ui(void) {
    video_draw_string(PLAY_X_OFFSET, UI_TEXT_Y, "SCORE", 17);
    score_draw(PLAY_X_OFFSET + 6 * TILE_W, UI_TEXT_Y, current_score);
}

void score_draw_hi_ui(void) {
    int x = PLAY_X_OFFSET + PLAY_W - 11 * TILE_W;  /* 2 + space + 6 cyfr + 2 margines */
    video_draw_string(x, UI_TEXT_Y, "HI", 17);
    score_draw(x + 3 * TILE_W, UI_TEXT_Y, hi_score);
}
