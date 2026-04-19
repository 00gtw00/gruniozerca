/*
 * sound.h — Interfejs dźwięku (abstrakcja SB / OPL2 / PC Speaker)
 * Gruniożerca DOS port
 *
 * Autodetekcja karty dźwiękowej:
 *   1. Sound Blaster (zmienna BLASTER=Axxx Iy Dz)
 *   2. AdLib / OPL2 (test port $388)
 *   3. PC Speaker (zawsze dostępny)
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* ---------- Typy kart dźwiękowych ----------------------------------------- */
typedef enum {
    SND_NONE    = 0,
    SND_SPEAKER = 1,  /* PC Speaker (port $61 + PIT) */
    SND_OPL2    = 2,  /* AdLib / OPL2 (port $388) */
    SND_OPL3    = 3,  /* OPL3 (port $388/$389) */
    SND_SB      = 4,  /* Sound Blaster (DSP, DMA) */
    SND_SB_PRO  = 5,  /* Sound Blaster Pro */
    SND_SB16    = 6   /* Sound Blaster 16 */
} SoundCardType;

/* ---------- ID muzyki (odpowiada nes/Muzyka/title.mus i kolejnosci) ------- */
typedef enum {
    MUS_TITLE    = 0,
    MUS_INGAME   = 1,
    MUS_GAMEOVER = 2,
    MUS_HISCORE  = 3,
    MUS_EMPTY    = 4,
    MUS_COUNT
} MusicTrack;

/* ---------- ID efektów dźwiękowych ---------------------------------------- */
typedef enum {
    SFX_CATCH   = 0,   /* złapanie marchewki */
    SFX_MISS    = 1,   /* spudłowanie */
    SFX_COMBO   = 2,   /* combo */
    SFX_GAMEOVER= 3,   /* koniec gry */
    SFX_COUNT
} SfxId;

/* ---------- Konfiguracja karty -------------------------------------------- */
typedef struct {
    SoundCardType type;
    uint16_t      port;   /* adres portu I/O ($220, $388, $61 itd.) */
    uint8_t       irq;    /* IRQ (SB: 5 lub 7) */
    uint8_t       dma;    /* kanał DMA (SB: 1 lub 3) */
    uint8_t       vol_music; /* głośność muzyki 0..100 */
    uint8_t       vol_sfx;   /* głośność efektów 0..100 */
} SoundConfig;

/* Aktualna konfiguracja (eksportowana dla SETUP) */
extern SoundConfig snd_config;

/* ---------- API abstrakcyjny ---------------------------------------------- */

/* Wykrywa kartę dźwiękową i inicjalizuje ją.
   Wypełnia snd_config. Zwraca wykryty typ. */
SoundCardType sound_detect(void);

/* Inicjalizacja z podaną konfiguracją (np. wczytaną z GRUNIO.CFG). */
int sound_init(const SoundConfig *cfg);

/* Deinicjalizacja — zatrzymaj muzykę, zwolnij zasoby. */
void sound_shutdown(void);

/* Uruchom utwór muzyczny (przechowywany w assets/music_N.opl lub PCM). */
void sound_play_music(MusicTrack track);

/* Zatrzymaj muzykę. */
void sound_stop_music(void);

/* Odegraj efekt dźwiękowy (nie blokuje). */
void sound_play_sfx(SfxId sfx);

/* Aktualizacja sekwencera muzycznego — WYWOŁAĆ CO KLATKĘ (lub z timera). */
void sound_update(void);

/* Ustaw głośność muzyki/sfx (0..100). */
void sound_set_vol_music(uint8_t vol);
void sound_set_vol_sfx(uint8_t vol);

/* Zwraca czytelną nazwę wykrytej karty (do SETUP). */
const char *sound_card_name(SoundCardType type);

/* Test dźwięku — gra krótki dźwięk testowy przez aktualną kartę. */
void sound_test(void);

#endif /* SOUND_H */
