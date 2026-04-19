/*
 * sound_speaker.c — Backend PC Speaker (port $61, PIT canal 2)
 * Gruniożerca DOS port, 2024
 *
 * PC Speaker: square wave przez PIT canal 2.
 * Muzyka: uproszczona mono melodia (tylko nuta na klatce).
 * SFX: krótkie tony o różnych częstotliwościach.
 */
#include "sound_speaker.h"
#include "dos_compat.h"
#include "timer.h"
#include <string.h>

/* ---------- Sterowanie głośnikiem ---------------------------------------- */
void spk_tone(uint16_t freq_hz) {
    if (freq_hz == 0) { spk_off(); return; }
#ifdef DOS_BUILD
    /* Divisor PIT canal 2: 1193182 / freq */
    uint16_t divisor = (uint16_t)(1193182U / (uint32_t)freq_hz);
    /* Skonfiguruj PIT canal 2 */
    outportb(0x43, 0xB6);                      /* canal 2, tryb 3 */
    outportb(0x42, (uint8_t)(divisor & 0xFF)); /* LSB */
    outportb(0x42, (uint8_t)(divisor >> 8));   /* MSB */
    /* Włącz głośnik: port $61 bit0=gate PIT2, bit1=speaker data */
    outportb(0x61, inportb(0x61) | 0x03);
#else
    (void)freq_hz;
#endif
}

void spk_off(void) {
#ifdef DOS_BUILD
    outportb(0x61, inportb(0x61) & 0xFC);
#endif
}

/* ---------- Prosta melodia muzyki ---------------------------------------- */
/* Tabela nut (Hz) dla melodii titlescreen — uproszczona */
static const uint16_t title_melody[] = {
    523, 523, 659, 659, 784, 784, 880, 0,
    880, 784, 784, 659, 659, 523, 523, 0,
    0
};
static const uint8_t  title_durations[] = {
    8, 8, 8, 8, 8, 8, 16, 8,
    8, 8, 8, 8, 8, 16, 8, 16,
    0
};

static int      mus_playing    = 0;
static int      mus_note_idx   = 0;
static uint8_t  mus_note_timer = 0;
static const uint16_t *mus_notes = NULL;
static const uint8_t  *mus_durs  = NULL;

/* ---------- SFX stany ----------------------------------------------------- */
static uint8_t sfx_timer = 0;

/* ---------- API ----------------------------------------------------------- */
int spk_init(const SoundConfig *cfg) {
    (void)cfg;
    spk_off();
    return 1;
}

void spk_shutdown(void) {
    spk_off();
}

void spk_play_music(MusicTrack track) {
    (void)track;
    /* Na razie tylko tytuł ma melodię */
    mus_notes    = title_melody;
    mus_durs     = title_durations;
    mus_note_idx = 0;
    mus_note_timer = 0;
    mus_playing  = 1;
    spk_tone(mus_notes[0]);
}

void spk_stop_music(void) {
    mus_playing = 0;
    sfx_timer   = 0;
    spk_off();
}

void spk_play_sfx(SfxId sfx) {
    switch (sfx) {
    case SFX_CATCH:   spk_tone(880); sfx_timer = 6;  break;
    case SFX_MISS:    spk_tone(220); sfx_timer = 6;  break;
    case SFX_COMBO:   spk_tone(1320); sfx_timer = 10; break;
    case SFX_GAMEOVER:spk_tone(110); sfx_timer = 30; break;
    default: break;
    }
}

void spk_update(void) {
    /* SFX timer (ma priorytet nad muzyką) */
    if (sfx_timer > 0) {
        sfx_timer--;
        if (sfx_timer == 0) {
            spk_off();
            /* Wznów muzykę jeśli gra */
            if (mus_playing && mus_notes)
                spk_tone(mus_notes[mus_note_idx]);
        }
        return;
    }

    /* Sekwencer muzyki */
    if (!mus_playing || !mus_notes) return;

    mus_note_timer++;
    if (mus_note_timer >= mus_durs[mus_note_idx]) {
        mus_note_timer = 0;
        mus_note_idx++;
        if (mus_notes[mus_note_idx] == 0 && mus_durs[mus_note_idx] == 0) {
            /* Koniec melodii — zapętl */
            mus_note_idx = 0;
        }
        spk_tone(mus_notes[mus_note_idx]);
    }
}
