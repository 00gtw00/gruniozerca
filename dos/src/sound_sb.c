/*
 * sound_sb.c — Backend Sound Blaster (DSP, DMA, 8-bit PCM)
 * Gruniożerca DOS port, 2024
 *
 * Odtwarzanie muzyki: PCM sample (TODO: konwersja .mus → PCM przez mus2opl)
 * Efekty: krótkie sample 8-bit PCM @ 22050 Hz
 *
 * Protokół DSP SB:
 *   - Reset: port BASE+6, wysyłamy 0x01, czekamy na 0xAA pod BASE+0xE
 *   - Komenda 0x10: Output 8-bit PCM sample (single)
 *   - Komenda 0xD1/0xD3: Speaker on/off
 *   - DMA 8-bit: kanal DMA 1 (SB) lub 3 (SB16)
 */
#include "sound_sb.h"
#include "sound_opl.h"
#include "dos_compat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint16_t sb_port = 0x220;
static uint8_t  sb_irq  = 5;
static uint8_t  sb_dma  = 1;
static int      opl_ready = 0;

/* Prosty bufor PCM dla efektów SFX (generowane sinusy) */
#define SFX_BUF_SIZE 2048
static uint8_t sfx_buf[SFX_BUF_SIZE];

/* ---- DSP helpers (z timeoutem, bez nieskończonych pętli) ---- */
static void dsp_write(uint8_t cmd) {
#ifdef DOS_BUILD
    for (int t = 0; t < 100000; t++) {
        if (!(inportb(sb_port + 0xC) & 0x80)) {
            outportb(sb_port + 0xC, cmd);
            return;
        }
    }
#else
    (void)cmd;
#endif
}

static uint8_t dsp_read(void) {
#ifdef DOS_BUILD
    for (int t = 0; t < 100000; t++) {
        if (inportb(sb_port + 0xE) & 0x80)
            return inportb(sb_port + 0xA);
    }
    return 0xFF; /* timeout */
#else
    return 0;
#endif
}

static int dsp_reset(void) {
#ifdef DOS_BUILD
    outportb(sb_port + 0x6, 1);
    for (volatile int i = 0; i < 100; i++) ;
    outportb(sb_port + 0x6, 0);
    for (int i = 0; i < 500; i++) {
        if (dsp_read() == 0xAA) return 1;
    }
    return 0;
#else
    return 1;
#endif
}

int sb_init(const SoundConfig *cfg) {
    sb_port = cfg->port;
    sb_irq  = cfg->irq;
    sb_dma  = cfg->dma;

    /* OPL2 zawsze — SB ma wbudowany YM3812 (muzyka OPL niezależna od DSP) */
    SoundConfig opl_cfg = *cfg;
    opl_cfg.port = 0x388;
    if (opl_init(&opl_cfg)) opl_ready = 1;

    /* DSP reset dla PCM SFX — opcjonalne, nie blokuje muzyki */
    if (!dsp_reset()) {
        printf("SB DSP: brak odpowiedzi @ 0x%X (PCM SFX wylaczone)\n", sb_port);
        return opl_ready ? 1 : 0;
    }
    dsp_write(0xD1); /* speaker on */
    return 1;
}

void sb_shutdown(void) {
    if (opl_ready) { opl_shutdown(); opl_ready = 0; }
    /* Wyzeruj wyjście DAC (0x80 = środek zakresu = cisza dla unsigned 8-bit) */
    dsp_write(0x10);   /* Direct 8-bit DAC output */
    dsp_write(0x80);
    dsp_write(0xD3);   /* speaker off */
    dsp_reset();
}

/* Odtwórz sample 8-bit PCM przez DSP (Direct mode, bez DMA — do SFX) */
static void sb_play_sample(uint8_t *data, uint16_t len, uint16_t rate) {
    /* Ustaw częstotliwość próbkowania: Time_constant = 256 - 1000000/rate */
    uint8_t tc = (uint8_t)(256 - (1000000 / rate));
    dsp_write(0x40);  /* Set time constant */
    dsp_write(tc);

    /* Output 8-bit PCM (Direct) — wolne, ale proste dla SFX */
    /* W pełnej implementacji: DMA transfer + ISR */
    for (uint16_t i = 0; i < len; i++) {
        dsp_write(0x10);       /* Direct DAC 8-bit */
        dsp_write(data[i]);
        /* Czekaj 1/rate — TODO: użyj timera PIT */
        volatile int delay = 1000000 / rate / 10;
        while (delay--) ;
    }
    (void)data;
}

/* Generuj prosty ton (square wave) jako sample 8-bit */
static void gen_tone(uint8_t *buf, uint16_t len, uint16_t freq, uint16_t rate) {
    uint16_t period = rate / freq;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = ((i % period) < period / 2) ? 200 : 56;
    }
}

void sb_play_music(MusicTrack track) {
    if (opl_ready) opl_play_music(track);
}

void sb_stop_music(void) {
    if (opl_ready) opl_stop_music();
}

void sb_play_sfx(SfxId sfx) {
    uint16_t freq = 440;
    uint16_t len  = 1024;
    switch (sfx) {
    case SFX_CATCH:   freq = 880; len = 512;  break;
    case SFX_MISS:    freq = 220; len = 512;  break;
    case SFX_COMBO:   freq = 1320; len = 768; break;
    case SFX_GAMEOVER:freq = 110; len = 2048; break;
    default: break;
    }
    gen_tone(sfx_buf, len, freq, 22050);
    sb_play_sample(sfx_buf, len, 22050);
}

void sb_update(void) {
    if (opl_ready) opl_update();
}
