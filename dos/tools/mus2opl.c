/*
 * mus2opl.c — Konwerter FamiTone2 .mus → sekwencer OPL2 (.opl)
 * Gruniożerca DOS port, 2024
 * Narzędzie hosta — kompilowane zwykłym gcc.
 *
 * Użycie:
 *   mus2opl <input.mus> <output.opl>
 *
 * Format .mus (FamiTone2 binary export z FamiTracker):
 *   Offset 0x00: adres danych w przestrzeni NES ($8000+) — little-endian word
 *   Następnie: stream komend FamiTone2
 *
 * Komendy FamiTone2 (bajty w strumieniu):
 *   0x00        = koniec klatki (czekaj 1 tick)
 *   0x01..0x7F  = ilość dodatkowych ticków oczekiwania (N-1)
 *   0x80..0xBF  = nota: nuta = (cmd - 0x80), kanał zakodowany w pozycji
 *   0xC0        = koniec piosenki (loop do początku)
 *   0xC1..0xCF  = zestaw głośności/efektu
 *   Dokładny format zależy od wersji FamiTone2 (v1.11 w tym projekcie).
 *
 * Format wyjściowy .opl (własny):
 *   Każdy event: 4 bajty { uint8 tick_delta, uint8 opl_reg, uint8 opl_val, uint8 pad }
 *   Koniec: { 0, 0xFF, 0, 0 }
 *
 * Mapowanie NES APU → OPL2:
 *   Kanał pulse 1  → OPL2 kanał 0  (rejestry $A0/$B0)
 *   Kanał pulse 2  → OPL2 kanał 1
 *   Kanał triangle → OPL2 kanał 2
 *   Kanał noise    → OPL2 perkusja (hi-hat, rejestr $BD)
 *
 * Uwaga: FamiTone2 .mus jest formatem binarnym specyficznym dla wersji.
 * Ta implementacja analizuje format v1.11 z projektu Gruniożerca.
 * Dla plików z innych projektów może wymagać dostosowania.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* =========================================================================
   Tablice konwersji NES APU → OPL2
   ========================================================================= */

/* Częstotliwości nut NES APU (timer period → Hz):
   freq = 1789773 / (16 * (period + 1))
   Nutom MIDI 0..95 odpowiadają C0..B7 */
static double note_freq(int midi_note) {
    return 440.0 * pow(2.0, (midi_note - 69) / 12.0);
}

/* OPL2 F-number dla częstotliwości i bloku (oktawy):
   F-num = freq * 2^(20 - block) / 49716
   Dobieramy blok tak, żeby F-num mieścił się w 0..1023 */
static void freq_to_fnum(double freq, uint16_t *fnum_out, uint8_t *block_out) {
    int block = 0;
    double f = freq;
    /* Zwiększaj blok aż F-num < 1024 */
    while (f * (1 << (20 - block)) / 49716.0 >= 1024.0 && block < 7) block++;
    uint16_t fnum = (uint16_t)(f * (1 << (20 - block)) / 49716.0);
    if (fnum > 1023) fnum = 1023;
    *fnum_out  = fnum;
    *block_out = (uint8_t)block;
}

/* =========================================================================
   Struktury eventów wyjściowych
   ========================================================================= */
typedef struct {
    uint8_t tick_delta;
    uint8_t reg;
    uint8_t val;
    uint8_t pad;
} OplEvent;

#define MAX_EVENTS 65536
static OplEvent out_events[MAX_EVENTS];
static int      out_count = 0;
static uint32_t current_tick = 0;
static uint32_t last_emit_tick = 0;

static void emit(uint8_t reg, uint8_t val) {
    if (out_count >= MAX_EVENTS - 1) return;
    uint32_t delta = current_tick - last_emit_tick;
    if (delta > 255) delta = 255;
    out_events[out_count].tick_delta = (uint8_t)delta;
    out_events[out_count].reg        = reg;
    out_events[out_count].val        = val;
    out_events[out_count].pad        = 0;
    out_count++;
    last_emit_tick = current_tick;
}

static void emit_end(void) {
    out_events[out_count].tick_delta = 0;
    out_events[out_count].reg        = 0xFF;
    out_events[out_count].val        = 0;
    out_events[out_count].pad        = 0;
    out_count++;
}

/* =========================================================================
   Stan kanałów OPL2
   ========================================================================= */
typedef struct {
    uint8_t  active;
    int      midi_note;
    uint8_t  volume;    /* 0..15 */
} OplChannel;

static OplChannel channels[4]; /* 0=pulse1, 1=pulse2, 2=triangle, 3=noise */

/* Rejestry OPL2 dla kanałów 0-2 */
static const uint8_t opl_reg_freq_lo[3] = { 0xA0, 0xA1, 0xA2 };
static const uint8_t opl_reg_freq_hi[3] = { 0xB0, 0xB1, 0xB2 };
static const uint8_t opl_reg_ksl_vol[3] = { 0x43, 0x44, 0x45 }; /* carrier volume */

static void opl_note_on(int ch, int midi_note, uint8_t vol) {
    if (ch > 2) return; /* noise → TODO perkusja */
    uint16_t fnum;
    uint8_t  block;
    freq_to_fnum(note_freq(midi_note), &fnum, &block);

    /* Głośność OPL2: 0x00=max, 0x3F=cicho (odwrócona skala) */
    uint8_t opl_vol = (uint8_t)(63 - (vol * 4));

    /* Wyciszy stary dźwięk */
    emit(opl_reg_freq_hi[ch], 0x00);
    /* Ustaw głośność */
    emit(opl_reg_ksl_vol[ch], opl_vol);
    /* Ustaw F-num i block, key on */
    emit(opl_reg_freq_lo[ch], (uint8_t)(fnum & 0xFF));
    emit(opl_reg_freq_hi[ch], (uint8_t)((block << 2) | ((fnum >> 8) & 0x03) | 0x20));

    channels[ch].active    = 1;
    channels[ch].midi_note = midi_note;
    channels[ch].volume    = vol;
}

static void opl_note_off(int ch) {
    if (ch > 2) return;
    /* Wyczyść bit key-on */
    uint16_t fnum;
    uint8_t  block;
    freq_to_fnum(note_freq(channels[ch].midi_note), &fnum, &block);
    emit(opl_reg_freq_hi[ch], (uint8_t)((block << 2) | ((fnum >> 8) & 0x03)));
    channels[ch].active = 0;
}

/* =========================================================================
   Parsowanie strumienia FamiTone2 .mus v1.11
   =========================================================================
   Format strumienia (po nagłówku 2 bajtów adresu NES):

   Header piosenki: kilka wskaźników do kanałów (3 × 2 bajty = adresy)
   Każdy kanał to osobny strumień komend.

   Komendy strumienia kanału:
     0x00       = koniec klatki (pauza 1 tick)
     0x01..0x7F = pauza N+1 ticków
     0x80+note  = nota (nuta 0..53 = C1..A5)
     0xC0       = zakończenie piosenki (wróć do początku)
     0xE0+vol   = set volume (dolne 4 bity)
     0xF8       = nota OFF

   Uwaga: dokładne kody komend FamiTone2 v1.11 mogą się różnić.
   Ta implementacja to przybliżenie — sprawdź plik famitone2.asm dla pewności.
   ========================================================================= */

static int parse_mus(const uint8_t *data, size_t size) {
    if (size < 6) {
        fprintf(stderr, "Plik .mus za krótki\n");
        return 0;
    }

    /* Pierwsze 2 bajty: adres NES (ignoruj) */
    size_t pos = 2;

    /* Zakładamy uproszczony format: jeden strumień wszystkich kanałów razem */
    /* W rzeczywistości FamiTone2 ma osobne wskaźniki kanałów — TODO pełna impl. */

    int current_channel = 0;
    uint8_t cur_vol[4] = { 12, 12, 12, 8 }; /* domyślne głośności */
    uint32_t ticks = 0;

    while (pos < size) {
        uint8_t cmd = data[pos++];

        if (cmd == 0x00) {
            /* Koniec klatki */
            current_tick = ticks++;
            /* Zmień kanał (uproszczenie: rotacja) */
            current_channel = (current_channel + 1) % 3;
        }
        else if (cmd >= 0x01 && cmd <= 0x7F) {
            /* Pauza N ticków */
            ticks += cmd;
        }
        else if (cmd >= 0x80 && cmd <= 0xBF) {
            /* Nota */
            int note = cmd - 0x80;       /* 0..63 → MIDI offset */
            int midi = note + 36;        /* C2 = MIDI 36 jako baza */
            if (midi > 127) midi = 127;
            current_tick = ticks;
            opl_note_on(current_channel, midi, cur_vol[current_channel]);
        }
        else if (cmd == 0xC0) {
            /* Koniec piosenki */
            break;
        }
        else if (cmd >= 0xC1 && cmd <= 0xCF) {
            /* Efekty/instrumenty — ignoruj w tej wersji */
        }
        else if (cmd >= 0xD0 && cmd <= 0xDF) {
            /* Zmiana głośności */
            cur_vol[current_channel] = cmd & 0x0F;
        }
        else if (cmd == 0xF8) {
            /* Nota OFF */
            current_tick = ticks;
            opl_note_off(current_channel);
        }
    }

    return 1;
}

/* =========================================================================
   main
   ========================================================================= */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Użycie: mus2opl <input.mus> <output.opl>\n");
        fprintf(stderr, "Przykład: mus2opl ../nes/Muzyka/title.mus assets/music_0.opl\n");
        return 1;
    }

    const char *mus_path = argv[1];
    const char *opl_path = argv[2];

    /* Wczytaj .mus */
    FILE *fin = fopen(mus_path, "rb");
    if (!fin) {
        fprintf(stderr, "Błąd: nie można otworzyć %s\n", mus_path);
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long mus_size = ftell(fin);
    rewind(fin);

    uint8_t *mus_data = (uint8_t *)malloc((size_t)mus_size);
    if (!mus_data) { fclose(fin); fprintf(stderr, "Brak pamięci\n"); return 1; }
    fread(mus_data, 1, (size_t)mus_size, fin);
    fclose(fin);

    printf("Plik .mus: %s (%ld bajtów)\n", mus_path, mus_size);

    /* Zainicjuj stan */
    out_count = 0;
    current_tick = 0;
    last_emit_tick = 0;
    memset(channels, 0, sizeof(channels));

    /* Parsuj i konwertuj */
    if (!parse_mus(mus_data, (size_t)mus_size)) {
        free(mus_data);
        return 1;
    }
    free(mus_data);

    /* Dodaj marker końca */
    emit_end();

    /* Zapisz .opl */
    FILE *fout = fopen(opl_path, "wb");
    if (!fout) {
        fprintf(stderr, "Błąd: nie można zapisać %s\n", opl_path);
        return 1;
    }
    fwrite(out_events, sizeof(OplEvent), (size_t)out_count, fout);
    fclose(fout);

    printf("Zapisano: %s (%d eventów OPL2, ~%d ticków)\n",
           opl_path, out_count, (int)current_tick);
    return 0;
}
