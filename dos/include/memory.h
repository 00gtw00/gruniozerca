/*
 * memory.h — Pool allocator (stały rozmiar bloków)
 * Gruniożerca DOS port
 *
 * W protected mode DJGPP malloc działa normalnie, ale dla deterministycznego
 * zachowania i uniknięcia fragmentacji używamy prostego pool allocatora.
 * Wszystkie dane gry (obiekty, bufory) alokowane z pre-zaalokowanych puli.
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

/* Rozmiar głównej puli (bajty) — 256 KB powinno wystarczyć */
#define MEM_POOL_SIZE (256 * 1024)

/* ---------- API ------------------------------------------------------------ */

/* Inicjalizacja głównej puli pamięci. Wywołaj przed wszystkim. */
void mem_init(void);

/* Alokacja z puli liniowej (nie ma free — reset całej puli). */
void *mem_alloc(size_t size);

/* Alokacja wyrównana do granicy align (potęga 2). */
void *mem_alloc_aligned(size_t size, size_t align);

/* Reset całej puli (używaj między ekranami gry). */
void mem_reset(void);

/* Zwraca ile bajtów puli zostało zużyte. */
size_t mem_used(void);

/* Zwraca ile bajtów puli jest dostępnych. */
size_t mem_free(void);

/* Awaryjne zakończenie z komunikatem gdy brak pamięci. */
void mem_panic(const char *msg);

#endif /* MEMORY_H */
