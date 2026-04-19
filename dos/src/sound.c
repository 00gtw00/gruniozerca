/*
 * sound.c — Wykrywanie karty i interfejs abstrakcyjny dźwięku
 * Gruniożerca DOS port, 2024
 *
 * Kolejność autodetekcji:
 *   1. Sound Blaster (zmienna środowiskowa BLASTER=Axxx Iy Dz)
 *   2. AdLib / OPL2 (test port $388 — odczyt stanu timera)
 *   3. PC Speaker (zawsze dostępny)
 */
#include "sound.h"
#include "sound_sb.h"
#include "sound_opl.h"
#include "sound_speaker.h"
#include "dos_compat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

SoundConfig snd_config;

/* Wskaźniki na aktywny backend */
static int (*backend_init)(const SoundConfig *) = NULL;
static void(*backend_shutdown)(void)             = NULL;
static void(*backend_play_music)(MusicTrack)     = NULL;
static void(*backend_stop_music)(void)           = NULL;
static void(*backend_play_sfx)(SfxId)            = NULL;
static void(*backend_update)(void)               = NULL;

/* =========================================================================
   Wykrywanie Sound Blaster przez zmienną środowiskową BLASTER
   Format: BLASTER=A220 I5 D1 [T3]
   ========================================================================= */
static int detect_sb(SoundConfig *out) {
    const char *blaster = getenv("BLASTER");
    if (!blaster) return 0;

    out->port = 0x220;
    out->irq  = 5;
    out->dma  = 1;
    out->type = SND_SB;

    const char *p = blaster;
    while (*p) {
        if (*p == 'A' || *p == 'a')
            out->port = (uint16_t)strtol(p + 1, NULL, 16);
        else if (*p == 'I' || *p == 'i')
            out->irq = (uint8_t)strtol(p + 1, NULL, 10);
        else if (*p == 'D' || *p == 'd')
            out->dma = (uint8_t)strtol(p + 1, NULL, 10);
        else if ((*p == 'T' || *p == 't') && *(p+1) >= '4')
            out->type = SND_SB16;
        p++;
        /* Przejdź do następnego tokenu */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return 1;
}

/* =========================================================================
   Wykrywanie AdLib / OPL2 przez test timera OPL2 na porcie $388
   ========================================================================= */
static int detect_opl2(SoundConfig *out) {
#ifndef DOS_BUILD
    return 0;
#else
    /* Reset timerów OPL2 */
    outportb(0x388, 0x04);   /* Timer control register */
    outportb(0x389, 0x60);   /* Reset timer 1 i 2 */
    outportb(0x389, 0x80);   /* Reset flagi */

    /* Odczytaj status przed testem */
    uint8_t stat1 = inportb(0x388);

    /* Ustaw timer 1 */
    outportb(0x388, 0x02);   /* Timer 1 preset */
    outportb(0x389, 0xFF);
    outportb(0x388, 0x04);   /* Start timer 1 */
    outportb(0x389, 0x21);

    /* Odczekaj ~80 µs — małe opóźnienie */
    for (volatile int i = 0; i < 1000; i++) ;

    uint8_t stat2 = inportb(0x388);

    /* Resetuj timery */
    outportb(0x388, 0x04);
    outportb(0x389, 0x60);
    outportb(0x389, 0x80);

    /* OPL2 obecny jeśli: stat1 bit5-7 = 0, stat2 bit5 = 1 */
    if ((stat1 & 0xE0) != 0 || (stat2 & 0xE0) != 0xC0)
        return 0;

    out->type = SND_OPL2;
    out->port = 0x388;
    out->irq  = 0;
    out->dma  = 0;
    return 1;
#endif
}

/* ========================================================================= */

SoundCardType sound_detect(void) {
    memset(&snd_config, 0, sizeof(snd_config));
    snd_config.vol_music = 80;
    snd_config.vol_sfx   = 70;

    /* 1. Sound Blaster */
    if (detect_sb(&snd_config)) {
        printf("Wykryto: Sound Blaster @ 0x%X IRQ%d DMA%d\n",
               snd_config.port, snd_config.irq, snd_config.dma);
        return snd_config.type;
    }

    /* 2. OPL2 / AdLib */
    if (detect_opl2(&snd_config)) {
        printf("Wykryto: AdLib/OPL2 @ 0x%X\n", snd_config.port);
        return SND_OPL2;
    }

    /* 3. PC Speaker — zawsze */
    snd_config.type = SND_SPEAKER;
    snd_config.port = 0x61;
    printf("Brak karty dźwiękowej — używam PC Speaker.\n");
    return SND_SPEAKER;
}

int sound_init(const SoundConfig *cfg) {
    memcpy(&snd_config, cfg, sizeof(snd_config));

    switch (cfg->type) {
    case SND_SB:
    case SND_SB_PRO:
    case SND_SB16:
        backend_init        = sb_init;
        backend_shutdown    = sb_shutdown;
        backend_play_music  = sb_play_music;
        backend_stop_music  = sb_stop_music;
        backend_play_sfx    = sb_play_sfx;
        backend_update      = sb_update;
        break;

    case SND_OPL2:
    case SND_OPL3:
        backend_init        = opl_init;
        backend_shutdown    = opl_shutdown;
        backend_play_music  = opl_play_music;
        backend_stop_music  = opl_stop_music;
        backend_play_sfx    = opl_play_sfx;
        backend_update      = opl_update;
        break;

    case SND_SPEAKER:
    default:
        backend_init        = spk_init;
        backend_shutdown    = spk_shutdown;
        backend_play_music  = spk_play_music;
        backend_stop_music  = spk_stop_music;
        backend_play_sfx    = spk_play_sfx;
        backend_update      = spk_update;
        break;
    }

    if (backend_init)
        return backend_init(&snd_config);
    return 0;
}

void sound_shutdown(void) {
    if (backend_shutdown) backend_shutdown();
    backend_init = NULL;
}

void sound_play_music(MusicTrack track) {
    if (backend_play_music) backend_play_music(track);
}

void sound_stop_music(void) {
    if (backend_stop_music) backend_stop_music();
}

void sound_play_sfx(SfxId sfx) {
    if (backend_play_sfx) backend_play_sfx(sfx);
}

void sound_update(void) {
    if (backend_update) backend_update();
}

void sound_set_vol_music(uint8_t vol) { snd_config.vol_music = vol; }
void sound_set_vol_sfx(uint8_t vol)   { snd_config.vol_sfx   = vol; }

const char *sound_card_name(SoundCardType type) {
    switch (type) {
    case SND_NONE:    return "Brak";
    case SND_SPEAKER: return "PC Speaker";
    case SND_OPL2:    return "AdLib/OPL2";
    case SND_OPL3:    return "OPL3";
    case SND_SB:      return "Sound Blaster";
    case SND_SB_PRO:  return "Sound Blaster Pro";
    case SND_SB16:    return "Sound Blaster 16";
    default:          return "Nieznana";
    }
}

void sound_test(void) {
    sound_play_sfx(SFX_CATCH);
}
