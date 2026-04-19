/*
 * sound_opl.c — Backend OPL2, odtwarzacz RAD v1
 * Gruniożerca DOS port, 2024
 *
 * Logika odtwarzacza oparta na RADPLAY 0.2.1 autorstwa Haydena Kroepfla
 * (ChartreuseK / VGASNOW, 2017 – licencja open source).
 * Zaadaptowano dla DJGPP (32-bit protected mode): usunięto far pointery,
 * obsługę przerwań Turbo C (getvect/setvect), dodano tick-rate conversion
 * (cel 50 Hz z wywołań 60 Hz z pętli gry).
 *
 * Format RAD v1.0:
 *   [16B sig "RAD by REALiTY!!"] [1B ver=0x10] [1B flags]
 *   flags: bit7=has_desc, bit6=slow(18.2Hz), bit4-0=speed(1-31)
 *   [optional null-term description]
 *   [instruments: [1B 1-based idx][11B patch], terminated 0x00]
 *   [order list: [1B count][count entries], entry&0x80 = jump]
 *   [pattern offsets: 32 × uint16_t LE absolute file pos]
 *   [pattern data: rest of file]
 */
#include "sound_opl.h"
#include "dos_compat.h"
#include "pack.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
   OPL2 I/O (port 0x388/0x389)
   ========================================================================= */

static uint16_t opl_base = 0x388;

void opl_write(uint8_t reg, uint8_t val) {
#ifdef DOS_BUILD
    outportb(opl_base,     reg);
    /* ~3.3 µs: 4 odczyty statusu */
    inportb(opl_base); inportb(opl_base);
    inportb(opl_base); inportb(opl_base);
    outportb(opl_base + 1, val);
    /* ~23 µs: 36 odczytów */
    for (int i = 0; i < 36; i++) inportb(opl_base);
#else
    (void)reg; (void)val;
#endif
}

static void opl_hw_reset(void) {
    for (int r = 1; r <= 0xF5; r++)
        opl_write((uint8_t)r, 0x00);
    opl_write(0x01, 0x20);   /* waveform select enable */
}

/* =========================================================================
   Stan odtwarzacza RAD (globale, tak jak w oryginale ChartreuseK)
   ========================================================================= */

#define CHANS       9
#define INSTCNT    31
#define INSTLEN    11

/* Offsety operatorów per kanał (mod = choff, car = choff+3) */
static const uint8_t al_choff[CHANS] = {
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};

/* F-numery dla C#..C (12 nut), oryginalnie z Reality AdLib Tracker */
static const uint16_t notefreq[12] = {
    0x16b, 0x181, 0x198, 0x1b0, 0x1ca, 0x1e5,
    0x202, 0x220, 0x241, 0x263, 0x287, 0x2ae
};

#define NOTE_C   0x156u
#define OCTAVE   (0x2aeu - NOTE_C)

#define linearfreq(oct, note_idx) \
    ((int)((unsigned)(oct) * OCTAVE) + (int)notefreq[(note_idx)] - (int)NOTE_C)
#define linearfreq2(oct, freq_raw) \
    ((int)((unsigned)(oct) * OCTAVE) + (int)(freq_raw) - (int)NOTE_C)

/* Instrument: 11 bajtów w kolejności jak w RAD (carrier-first per para) */
typedef struct {
    uint8_t r23, r20;   /* carrier/mod AVEKM  (reg 0x20+choff) */
    uint8_t r43, r40;   /* carrier/mod KSL/TL (reg 0x40+choff) */
    uint8_t r63, r60;   /* carrier/mod AR/DR  (reg 0x60+choff) */
    uint8_t r83, r80;   /* carrier/mod SL/RR  (reg 0x80+choff) */
    uint8_t rC0;        /* feedback/connection (reg 0xC0+chan)  */
    uint8_t rE3, rE0;   /* carrier/mod waveform (reg 0xE0+choff) */
} Inst;

static Inst     insts[INSTCNT];

static uint8_t  order[128];
static uint8_t  orderlen;
static uint8_t  curorder;
static uint8_t  curpat;
static uint8_t  curline;
static uint8_t  rad_speed;
static uint8_t  spdcnt;

static uint8_t  *rad_data   = NULL;
static uint16_t  rad_datalen = 0;
static uint16_t  patoff[32];        /* relative to rad_data[0] */
static uint16_t  patpos;            /* cursor w rad_data[] */

/* Stan per kanał (potrzebny do efektów — OPL2 nie ma odczytu rejestrów) */
static uint8_t  prev_vol[CHANS];
static uint8_t  prev_freqlo[CHANS];
static uint8_t  prev_freqhi[CHANS];
static uint16_t toneslide_dst[CHANS];
static uint8_t  toneslide_spd[CHANS];

static struct {
    int8_t  portslide;
    uint8_t toneslide;
    int8_t  volslide;
} efx[CHANS];

static uint8_t  rad_playing = 0;

/* Tick-rate conversion: cel 50 Hz, wywołania 60 Hz */
static uint32_t tick_accum  = 0;
/* 50/60 * 65536 = 54613 */
#define TICK_FP  54613u

/* =========================================================================
   Helpers: instrument, nuta, częstotliwość, głośność
   ========================================================================= */

static void load_inst(uint8_t idx, uint8_t chan) {
    uint8_t co = al_choff[chan];
    const Inst *p = &insts[idx];
    opl_write(0x23 + co, p->r23);
    opl_write(0x20 + co, p->r20);
    opl_write(0x43 + co, p->r43);  prev_vol[chan] = p->r43;
    opl_write(0x40 + co, p->r40);
    opl_write(0x63 + co, p->r63);
    opl_write(0x60 + co, p->r60);
    opl_write(0x83 + co, p->r83);
    opl_write(0x80 + co, p->r80);
    opl_write(0xE3 + co, p->rE3);
    opl_write(0xE0 + co, p->rE0);
    /* OPL3: bity 4-5 rejestru 0xC0 = wyjście L+R; w OPL2 bity te są ignorowane */
    opl_write(0xC0 + chan, p->rC0 | 0x30u);
}

/* Załaduj surowe 11 bajtów patcha (kolejność jak w Inst) do kanału */
static void load_inst_raw(uint8_t chan, const uint8_t *p) {
    uint8_t co = al_choff[chan];
    opl_write(0x23 + co, p[0]);
    opl_write(0x20 + co, p[1]);
    opl_write(0x43 + co, p[2]);  prev_vol[chan] = p[2];
    opl_write(0x40 + co, p[3]);
    opl_write(0x63 + co, p[4]);
    opl_write(0x60 + co, p[5]);
    opl_write(0x83 + co, p[6]);
    opl_write(0x80 + co, p[7]);
    opl_write(0xE3 + co, p[9]);
    opl_write(0xE0 + co, p[10]);
    opl_write(0xC0 + chan, p[8] | 0x30u);
}

static void set_note(uint8_t chan, uint8_t oct, uint8_t note) {
    uint16_t freq;
    if (!note) return;
    if (note < 13) {
        freq = (uint16_t)(0x2000u | ((uint16_t)oct << 10) | notefreq[note - 1]);
        prev_freqlo[chan] = (uint8_t)freq;
        prev_freqhi[chan] = (uint8_t)(freq >> 8);
        opl_write(0xA0 + chan, (uint8_t)freq);
        opl_write(0xB0 + chan, (uint8_t)(freq >> 8));
    } else {
        /* KEY OFF */
        prev_freqhi[chan] &= ~0x20u;
        opl_write(0xB0 + chan, prev_freqhi[chan]);
    }
}

static void set_linear_freq(uint8_t chan, int lf) {
    uint8_t  oct;
    uint16_t nf, freq;
    if (lf < 0) lf = 0;
    oct  = (uint8_t)((unsigned)lf / OCTAVE);
    nf   = (uint16_t)((unsigned)lf % OCTAVE) + NOTE_C;
    freq = (uint16_t)((prev_freqhi[chan] & ~0x1Fu) << 8) | nf | ((uint16_t)oct << 10);
    prev_freqlo[chan] = (uint8_t)freq;
    prev_freqhi[chan] = (uint8_t)(freq >> 8);
    opl_write(0xA0 + chan, (uint8_t)freq);
    opl_write(0xB0 + chan, (uint8_t)(freq >> 8));
}

static int get_linear_freq(uint8_t chan) {
    uint16_t freq = (uint16_t)prev_freqlo[chan] | ((uint16_t)prev_freqhi[chan] << 8);
    uint8_t  oct  = (uint8_t)((freq >> 10) & 0x7u);
    uint16_t nf   = freq & 0x3FFu;
    return linearfreq2(oct, nf);
}

static void set_volume(uint8_t chan, int vol) {
    uint8_t v;
    if (vol > 63) vol = 63;
    if (vol <  0) vol =  0;
    v = (uint8_t)((prev_vol[chan] & ~0x3Fu) | ((uint8_t)vol ^ 0x3Fu));
    prev_vol[chan] = v;
    opl_write(0x43 + al_choff[chan], v);
}

static int get_volume(uint8_t chan) {
    return (int)((prev_vol[chan] & 0x3Fu) ^ 0x3Fu);
}

/* =========================================================================
   Komendy efektów RAD
   ========================================================================= */
#define CMD_PORTUP        1
#define CMD_PORTDN        2
#define CMD_TONESLIDE     3
#define CMD_TONEVOLSLIDE  5
#define CMD_VOLSLIDE     10
#define CMD_SETVOL       12
#define CMD_JMPLINE      13
#define CMD_SETSPEED     15

/* Zwraca 0 lub (1+jumpline) gdy komenda CMD_JMPLINE */
static int do_note(uint8_t chan, uint8_t oct, uint8_t note,
                   uint8_t cmd, uint8_t param, uint8_t inst) {
    if (note) {
        if (cmd == CMD_TONESLIDE) {
            toneslide_dst[chan] = (uint16_t)linearfreq(oct, note - 1);
            if (param) toneslide_spd[chan] = param;
            efx[chan].toneslide = 1;
            return 0;
        }
        set_note(chan, oct, 15);       /* KEY OFF poprzedniej nuty */
        if (inst) load_inst(inst - 1, chan);
        set_note(chan, oct, note);
    }
    switch (cmd) {
    case CMD_PORTUP:       efx[chan].portslide =  (int8_t)param; break;
    case CMD_PORTDN:       efx[chan].portslide = -(int8_t)param; break;
    case CMD_TONESLIDE:    efx[chan].toneslide = 1;
                           if (param) toneslide_spd[chan] = param;
                           break;
    case CMD_TONEVOLSLIDE: efx[chan].toneslide = 1;   /* fall-through */
    case CMD_VOLSLIDE:
        efx[chan].volslide = (int8_t)((param < 50) ? -(int)param : (int)(param - 50));
        break;
    case CMD_SETVOL:    set_volume(chan, param); break;
    case CMD_JMPLINE:   return 1 + (int)param;
    case CMD_SETSPEED:  rad_speed = param; break;
    }
    return 0;
}

static void doeffects(void) {
    uint8_t ch;
    int lf, vol;
    for (ch = 0; ch < CHANS; ch++) {
        if (efx[ch].portslide) {
            lf = get_linear_freq(ch) + efx[ch].portslide;
            set_linear_freq(ch, lf);
        }
        if (efx[ch].toneslide) {
            lf = get_linear_freq(ch);
            if (lf < (int)toneslide_dst[ch]) {
                lf += toneslide_spd[ch];
                if (lf >= (int)toneslide_dst[ch]) { lf = toneslide_dst[ch]; efx[ch].toneslide = 0; }
            } else if (lf > (int)toneslide_dst[ch]) {
                lf -= toneslide_spd[ch];
                if (lf <= (int)toneslide_dst[ch]) { lf = toneslide_dst[ch]; efx[ch].toneslide = 0; }
            } else {
                efx[ch].toneslide = 0;
            }
            set_linear_freq(ch, lf);
        }
        if (efx[ch].volslide) {
            vol = get_volume(ch) + efx[ch].volslide;
            if (vol < 0) vol = 0;
            set_volume(ch, vol);
        }
    }
}

/* =========================================================================
   Główny tick odtwarzacza (~50 Hz)
   Przeniesiony z void interrupt play() — bez zmian logiki.
   ========================================================================= */
static void rad_tick(void) {
    uint8_t line, chan, note0, note1, param, cmd, oct, n, inst;
    int     nextline;
    uint8_t ci;

    if (patpos == 0xFFFFu) { rad_playing = 0; return; }

    if (spdcnt-- == 0) {
        /* Resetuj efekty na początku każdego wiersza */
        for (ci = 0; ci < CHANS; ci++) {
            efx[ci].portslide = 0;
            efx[ci].toneslide = 0;
            efx[ci].volslide  = 0;
        }

        line = rad_data[patpos];

        if (curline++ == (line & 0x7Fu)) {
            patpos++;
            do {
                if (patpos + 2 >= rad_datalen) break;
                chan  = rad_data[patpos++];
                note0 = rad_data[patpos++];
                note1 = rad_data[patpos++];
                param = 0;
                if ((note1 & 0x0Fu) && patpos < rad_datalen)
                    param = rad_data[patpos++];

                cmd  = note1 & 0x0Fu;
                oct  = (note0 >> 4) & 0x07u;
                n    = note0 & 0x0Fu;
                inst = (uint8_t)((note1 >> 4) | ((note0 & 0x80u) >> 3));

                nextline = do_note(chan & 0x7Fu, oct, n, cmd, param, inst);
                if (nextline > 0) {
                    curpat = order[++curorder];
                    while (curpat & 0x80u) {
                        curorder = curpat - 0x80u;
                        curpat   = order[curorder];
                    }
                    patpos = patoff[curpat];
                    /* Przeskocz do wiersza docelowego */
                    while (patpos < rad_datalen &&
                           (rad_data[patpos] & 0x7Fu) < (uint8_t)(nextline - 1)) {
                        uint8_t lb = rad_data[patpos];
                        if (lb & 0x80u) { patpos = 0xFFFFu; break; }
                        patpos++;
                        while (patpos < rad_datalen) {
                            uint8_t cb2 = rad_data[patpos++];
                            if (patpos + 1 < rad_datalen) {
                                uint8_t nb2 = rad_data[patpos + 1];
                                patpos += 2;
                                if ((nb2 & 0x0Fu) && patpos < rad_datalen) patpos++;
                            }
                            if (cb2 & 0x80u) break;
                        }
                    }
                    curline = (uint8_t)(nextline - 1);
                    goto skip;
                }
            } while (!(chan & 0x80u));
        }

        /* Koniec patternu */
        if ((line & 0x80u) || (curline >= 0x80u)) {
            curpat = order[++curorder];
            while (curpat & 0x80u) {
                curorder = curpat - 0x80u;
                curpat   = order[curorder];
            }
            patpos  = patoff[curpat];
            curline = 0;
        }
skip:
        spdcnt = rad_speed - 1;
    }

    doeffects();
}

/* =========================================================================
   Ładowanie RAD z bufora RAM (gdy plik pochodzi z archiwum GRUNIO.DAT)
   ========================================================================= */
typedef struct { const uint8_t *data; uint32_t pos, size; } RMC;
static int  rmc_getc(RMC *m) { return m->pos < m->size ? m->data[m->pos++] : EOF; }
static size_t rmc_read(RMC *m, void *buf, size_t n) {
    size_t av = m->size - m->pos;
    if (n > av) n = av;
    memcpy(buf, m->data + m->pos, n);
    m->pos += (uint32_t)n;
    return n;
}

static int rad_load_mem(const uint8_t *src, uint32_t src_size) {
    RMC      mc;
    uint8_t  buf[18];
    int      ch, i;
    uint16_t dataoff;
    long     fsize;

    if (rad_data) { free(rad_data); rad_data = NULL; }

    mc.data = src; mc.pos = 0; mc.size = src_size;

    if (rmc_read(&mc, buf, 18) != 18)                    return 0;
    if (buf[0] != 'R' || buf[1] != 'A' || buf[2] != 'D') return 0;
    if (buf[0x10] != 0x10)                               return 0;

    rad_speed = buf[0x11] & 0x1Fu;
    if (!rad_speed) rad_speed = 6;

    if (buf[0x11] & 0x80u)
        while ((ch = rmc_getc(&mc)) != 0 && ch != EOF) ;

    memset(insts, 0, sizeof(insts));
    while ((ch = rmc_getc(&mc)) != 0 && ch != EOF) {
        if (ch >= 1 && ch <= INSTCNT)
            rmc_read(&mc, &insts[ch - 1], INSTLEN);
        else
            mc.pos += INSTLEN;
    }

    ch = rmc_getc(&mc);
    if (ch == EOF) return 0;
    orderlen = (uint8_t)ch;
    if (rmc_read(&mc, order, orderlen) != (size_t)orderlen) return 0;

    for (i = 0; i < 32; i++) {
        int lo = rmc_getc(&mc), hi = rmc_getc(&mc);
        if (lo == EOF || hi == EOF) return 0;
        patoff[i] = (uint16_t)(lo | (hi << 8));
    }

    dataoff = (uint16_t)mc.pos;
    fsize   = (long)(mc.size - mc.pos);
    if (fsize <= 0 || fsize > 65535L) return 0;

    rad_data = (uint8_t *)malloc((size_t)fsize);
    if (!rad_data) return 0;
    rad_datalen = (uint16_t)rmc_read(&mc, rad_data, (size_t)fsize);

    for (i = 0; i < 32; i++)
        if (patoff[i]) patoff[i] -= dataoff;

    return 1;
}

/* =========================================================================
   Ładowanie pliku RAD
   ========================================================================= */
static int rad_load(const char *path) {
    FILE    *fp;
    uint8_t  buf[18];
    int      ch, i;
    long     old, fsize;
    uint16_t dataoff;

    /* Próbuj najpierw z archiwum GRUNIO.DAT */
    {
        uint32_t psz = 0;
        const uint8_t *pdata = pack_get_path(path, &psz);
        if (pdata) return rad_load_mem(pdata, psz);
    }

    /* Fallback: plik na dysku */
    if (rad_data) { free(rad_data); rad_data = NULL; }

    fp = fopen(path, "rb");
    if (!fp) return 0;

    if (fread(buf, 1, 18, fp) != 18)                   goto fail;
    if (buf[0] != 'R' || buf[1] != 'A' || buf[2] != 'D') goto fail;
    if (buf[0x10] != 0x10) goto fail;                  /* tylko v1.0 */

    rad_speed = buf[0x11] & 0x1Fu;
    if (!rad_speed) rad_speed = 6;
    /* bit6 = slow (18.2 Hz) — pomijamy, zakładamy 50 Hz */

    if (buf[0x11] & 0x80u) {
        while ((ch = fgetc(fp)) != 0 && ch != EOF) ;   /* pomiń opis */
    }

    /* Instrumenty: [1-based idx][11B patch], koniec = 0x00 */
    memset(insts, 0, sizeof(insts));
    while ((ch = fgetc(fp)) != 0 && ch != EOF) {
        if (ch >= 1 && ch <= INSTCNT)
            fread(&insts[ch - 1], 1, INSTLEN, fp);
        else
            fseek(fp, INSTLEN, SEEK_CUR);
    }

    /* Lista kolejności: [1B count][count wpisów] */
    ch = fgetc(fp);
    if (ch == EOF) goto fail;
    orderlen = (uint8_t)ch;
    if (fread(order, 1, orderlen, fp) != (size_t)orderlen) goto fail;

    /* Tablica offsetów patternów: 32 × uint16_t LE */
    for (i = 0; i < 32; i++) {
        int lo = fgetc(fp), hi = fgetc(fp);
        if (lo == EOF || hi == EOF) goto fail;
        patoff[i] = (uint16_t)(lo | (hi << 8));
    }

    /* Dane patternów: reszta pliku */
    old   = ftell(fp);
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp) - old;
    fseek(fp, old, SEEK_SET);
    if (fsize <= 0 || fsize > 65535L) goto fail;

    dataoff    = (uint16_t)old;
    rad_data   = (uint8_t *)malloc((size_t)fsize);
    if (!rad_data) goto fail;
    rad_datalen = (uint16_t)fread(rad_data, 1, (size_t)fsize, fp);
    fclose(fp);

    /* Przelicz offsety na relatywne do rad_data[0] */
    for (i = 0; i < 32; i++)
        if (patoff[i]) patoff[i] -= dataoff;

    return 1;
fail:
    fclose(fp);
    return 0;
}

/* =========================================================================
   Publiczne API
   ========================================================================= */

int opl_init(const SoundConfig *cfg) {
    opl_base = cfg->port;
    memset(prev_vol,       0, sizeof(prev_vol));
    memset(prev_freqlo,    0, sizeof(prev_freqlo));
    memset(prev_freqhi,    0, sizeof(prev_freqhi));
    memset(toneslide_dst,  0, sizeof(toneslide_dst));
    memset(toneslide_spd,  0, sizeof(toneslide_spd));
    memset(efx,            0, sizeof(efx));
    rad_playing = 0;
    opl_hw_reset();

    return 1;
}

void opl_shutdown(void) {
    opl_stop_music();
    opl_hw_reset();   /* zeruje wszystkie rejestry — cisza na wyjściu DAC */
    if (rad_data) { free(rad_data); rad_data = NULL; }
}

static const char * const music_files[MUS_COUNT] = {
    "assets/title.rad",     /* MUS_TITLE    */
    "assets/ingame.rad",    /* MUS_INGAME   */
    "assets/gamovr.rad",    /* MUS_GAMEOVER */
    "assets/hiscore.rad",   /* MUS_HISCORE  */
    NULL,                    /* MUS_EMPTY    */
};

void opl_play_music(MusicTrack track) {
    opl_stop_music();
    if ((unsigned)track >= MUS_COUNT) return;
    if (!music_files[track])           return;
    if (!rad_load(music_files[track])) return;
    if (orderlen == 0)                 return;

    memset(prev_vol,      0, sizeof(prev_vol));
    memset(prev_freqlo,   0, sizeof(prev_freqlo));
    memset(prev_freqhi,   0, sizeof(prev_freqhi));
    memset(toneslide_dst, 0, sizeof(toneslide_dst));
    memset(toneslide_spd, 0, sizeof(toneslide_spd));
    memset(efx,           0, sizeof(efx));

    curorder  = 0;
    curpat    = order[curorder];
    while (curpat & 0x80u) {
        curorder = curpat - 0x80u;
        curpat   = order[curorder];
    }
    patpos     = patoff[curpat];
    curline    = 0;
    spdcnt     = rad_speed - 1;
    tick_accum = 0;
    rad_playing = 1;
}

void opl_stop_music(void) {
    rad_playing = 0;
    opl_hw_reset();   /* zeruje wszystkie rejestry — natychmiastowa cisza */
}

void opl_update(void) {
    if (!rad_playing) return;
    /* Konwersja tempa: 60 wywołań/s → 50 ticków/s */
    tick_accum += TICK_FP;
    while (tick_accum >= 65536u) {
        tick_accum -= 65536u;
        if (rad_playing) rad_tick();
    }
}

/* =========================================================================
   SFX — kanał 8, krótki "blip" przez OPL2
   ========================================================================= */
void opl_play_sfx(SfxId sfx) {
    /* Patch w kolejności pól Inst: r23 r20 r43 r40 r63 r60 r83 r80 rC0 rE3 rE0 */
    static const uint8_t blip[INSTLEN] = {
        0x01, 0x01,   /* car/mod AVEKM: MULT=1                  */
        0x00, 0x2F,   /* car TL=0 (max vol), mod TL=47          */
        0xF0, 0xF0,   /* car/mod AR=F, DR=0                     */
        0x0F, 0x0F,   /* car/mod SL=0, RR=F (szybki release)    */
        0x00,         /* FM, feedback=0                          */
        0x00, 0x00    /* car/mod: sine wave                      */
    };
    load_inst_raw(8, blip);
    efx[8].toneslide = 0;  /* wymuś reprogramowanie przy powrocie muzyki */

    switch (sfx) {
    case SFX_CATCH:    set_note(8, 5, 9);  break;   /* A5 */
    case SFX_MISS:     set_note(8, 2, 1);  break;   /* C#2 */
    case SFX_COMBO:    set_note(8, 5, 5);  break;   /* E5 */
    case SFX_GAMEOVER: set_note(8, 1, 1);  break;   /* C#1 */
    default: break;
    }
}
