/*
 * pack.c — Czytnik archiwum GRUNIO.DAT
 * Gruniożerca DOS port
 *
 * Ładuje cały plik .DAT do RAM przy starcie; pack_get() zwraca
 * wskaźnik do danych (zero-copy) bez dalszego I/O na dysk.
 */
#include "pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Format archiwum (musi być zgodny z mkpack.c) ---- */
#define PACK_MAGIC    "GRPK"
#define NAME_LEN      16
#define TOC_ENTRY     24   /* NAME_LEN + 4 + 4 */
#define HDR_SIZE       8

typedef struct {
    char     name[NAME_LEN];
    uint32_t offset;
    uint32_t size;
} TocEntry;

static uint8_t  *s_data     = NULL;  /* cała zawartość GRUNIO.DAT w RAM */
static uint32_t  s_data_len = 0;
static TocEntry *s_toc      = NULL;
static int       s_num      = 0;

/* Wczytaj uint16_t little-endian z bufora */
static uint16_t read16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* Wczytaj uint32_t little-endian z bufora */
static uint32_t read32le(const uint8_t *p) {
    return (uint32_t)(p[0] | ((uint32_t)p[1]<<8) |
                             ((uint32_t)p[2]<<16) |
                             ((uint32_t)p[3]<<24));
}

int pack_init(void) {
    FILE    *f;
    long     fsize;
    uint16_t num;
    int      i;

    f = fopen(PACK_FILE, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    rewind(f);

    s_data = (uint8_t *)malloc((size_t)fsize);
    if (!s_data) { fclose(f); return 0; }

    if ((long)fread(s_data, 1, (size_t)fsize, f) != fsize) {
        free(s_data); s_data = NULL;
        fclose(f); return 0;
    }
    fclose(f);
    s_data_len = (uint32_t)fsize;

    /* Walidacja nagłówka */
    if (fsize < HDR_SIZE || memcmp(s_data, PACK_MAGIC, 4) != 0) {
        free(s_data); s_data = NULL;
        return 0;
    }

    num    = read16le(s_data + 4);
    s_num  = (int)num;

    if (fsize < (long)(HDR_SIZE + s_num * TOC_ENTRY)) {
        free(s_data); s_data = NULL;
        return 0;
    }

    /* Zbuduj tablicę TOC (wskaźniki do danych wewnątrz s_data) */
    s_toc = (TocEntry *)malloc((size_t)(s_num) * sizeof(TocEntry));
    if (!s_toc) { free(s_data); s_data = NULL; return 0; }

    for (i = 0; i < s_num; i++) {
        const uint8_t *entry = s_data + HDR_SIZE + i * TOC_ENTRY;
        memcpy(s_toc[i].name, entry, NAME_LEN);
        s_toc[i].name[NAME_LEN - 1] = '\0';
        s_toc[i].offset = read32le(entry + NAME_LEN);
        s_toc[i].size   = read32le(entry + NAME_LEN + 4);
    }

    return 1;
}

void pack_shutdown(void) {
    free(s_toc);  s_toc = NULL;
    free(s_data); s_data = NULL;
    s_num = 0;
    s_data_len = 0;
}

int pack_loaded(void) {
    return (s_data != NULL);
}

const uint8_t *pack_get_path(const char *path, uint32_t *out_size) {
    const char *base = path;
    const char *s;
    for (s = path; *s; s++) {
        if (*s == '/' || *s == '\\') base = s + 1;
    }
    return pack_get(base, out_size);
}

const uint8_t *pack_get(const char *name, uint32_t *out_size) {
    int i;
    if (!s_data || !name) return NULL;

    for (i = 0; i < s_num; i++) {
        if (strcmp(s_toc[i].name, name) == 0) {
            if (out_size) *out_size = s_toc[i].size;
            /* Sprawdź czy offset + size mieści się w załadowanych danych */
            if (s_toc[i].offset + s_toc[i].size > s_data_len) return NULL;
            return s_data + s_toc[i].offset;
        }
    }
    return NULL;
}
