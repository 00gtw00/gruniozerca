/*
 * mkpack.c — narzędzie do tworzenia GRUNIO.DAT
 * Kompilacja host-side (gcc): gcc -O2 mkpack.c -o mkpack
 *
 * Format GRUNIO.DAT:
 *   [0..3]  magic "GRPK"
 *   [4..5]  uint16_t num_files  (little-endian)
 *   [6..7]  zarezerwowane (0x00 0x00)
 *   [8..]   TOC: num_files × 24 bajty
 *             char[16]  name     (nazwa pliku bez ścieżki, zero-padded)
 *             uint32_t  offset   (od początku pliku .DAT)
 *             uint32_t  size     (rozmiar danych w bajtach)
 *   po TOC: dane plików (sklejone jeden po drugim)
 *
 * Użycie:
 *   mkpack GRUNIO.DAT plik1.dat plik2.pcx ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PACK_MAGIC   "GRPK"
#define NAME_LEN     16
#define TOC_ENTRY    24   /* NAME_LEN + 4 + 4 */
#define HDR_SIZE     8

static const char *basename_of(const char *path) {
    const char *s = path;
    const char *last = path;
    for (; *s; s++) {
        if (*s == '/' || *s == '\\') last = s + 1;
    }
    return last;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Użycie: %s WYJSCIE.DAT plik1 [plik2 ...]\n", argv[0]);
        return 1;
    }

    int num_files = argc - 2;
    char *out_path = argv[1];
    char **in_paths = argv + 2;

    /* Sprawdź pliki wejściowe i pobierz rozmiary */
    uint32_t *sizes = calloc(num_files, sizeof(uint32_t));
    for (int i = 0; i < num_files; i++) {
        FILE *f = fopen(in_paths[i], "rb");
        if (!f) {
            fprintf(stderr, "Błąd: nie można otworzyć %s\n", in_paths[i]);
            free(sizes);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        sizes[i] = (uint32_t)ftell(f);
        fclose(f);
    }

    /* Oblicz offsety danych (dane zaczynają się za nagłówkiem + TOC) */
    uint32_t data_start = (uint32_t)(HDR_SIZE + num_files * TOC_ENTRY);
    uint32_t *offsets = calloc(num_files, sizeof(uint32_t));
    uint32_t cur = data_start;
    for (int i = 0; i < num_files; i++) {
        offsets[i] = cur;
        cur += sizes[i];
    }

    /* Zapisz plik wyjściowy */
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Błąd: nie można otworzyć %s do zapisu\n", out_path);
        free(sizes); free(offsets);
        return 1;
    }

    /* Nagłówek */
    uint16_t n16 = (uint16_t)num_files;
    fwrite(PACK_MAGIC, 1, 4, out);
    fwrite(&n16, 2, 1, out);
    fputc(0, out); fputc(0, out);

    /* TOC */
    for (int i = 0; i < num_files; i++) {
        char name[NAME_LEN];
        memset(name, 0, NAME_LEN);
        strncpy(name, basename_of(in_paths[i]), NAME_LEN - 1);
        fwrite(name, 1, NAME_LEN, out);
        fwrite(&offsets[i], 4, 1, out);
        fwrite(&sizes[i],   4, 1, out);
    }

    /* Dane */
    for (int i = 0; i < num_files; i++) {
        FILE *f = fopen(in_paths[i], "rb");
        uint8_t buf[4096];
        size_t rd;
        while ((rd = fread(buf, 1, sizeof(buf), f)) > 0)
            fwrite(buf, 1, rd, out);
        fclose(f);
    }

    fclose(out);

    /* Podsumowanie */
    printf("Zapisano %s: %d pliki/plików, %u bajtów łącznie\n",
           out_path, num_files, cur);
    for (int i = 0; i < num_files; i++) {
        printf("  %-16s  offset=%6u  size=%6u\n",
               basename_of(in_paths[i]), offsets[i], sizes[i]);
    }

    free(sizes); free(offsets);
    return 0;
}
