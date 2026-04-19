/*
 * pack.h — Czytnik archiwum GRUNIO.DAT
 * Gruniożerca DOS port
 *
 * GRUNIO.DAT zawiera wszystkie asset'y gry (grafika, dźwięk, muzyka).
 * Cały plik jest ładowany do RAM przy starcie; zasoby są dostępne
 * przez wskaźnik (zero-copy) przez cały czas trwania gry.
 *
 * Format archiwum: patrz tools/mkpack.c
 */
#ifndef PACK_H
#define PACK_H

#include <stdint.h>
#include <stddef.h>

#define PACK_FILE   "GRUNIO.DAT"

/* Inicjalizacja — ładuje cały PACK_FILE do pamięci.
   Zwraca 1 jeśli OK, 0 jeśli nie można otworzyć pliku. */
int pack_init(void);

/* Zwalnia pamięć archiwum (wywołaj przy zamknięciu). */
void pack_shutdown(void);

/* Zwraca wskaźnik do danych zasobu o podanej nazwie (bez ścieżki)
   i zapisuje rozmiar w *out_size.
   Zwraca NULL jeśli zasób nie istnieje w archiwum. */
const uint8_t *pack_get(const char *name, uint32_t *out_size);

/* Sprawdza czy archiwum jest załadowane (pack_init zakończyło się sukcesem). */
int pack_loaded(void);

/* Jak pack_get(), ale akceptuje pełną ścieżkę (np. "assets/sprites.dat") —
   automatycznie odcina katalogi i szuka tylko po nazwie pliku. */
const uint8_t *pack_get_path(const char *path, uint32_t *out_size);

#endif /* PACK_H */
