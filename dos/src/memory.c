/*
 * memory.c — Pool allocator liniowy
 * Gruniożerca DOS port, 2024
 */
#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef DOS_BUILD
#  include <dpmi.h>
#endif

static uint8_t  pool[MEM_POOL_SIZE];
static size_t   pool_ptr = 0;

void mem_init(void) {
    pool_ptr = 0;
    memset(pool, 0, sizeof(pool));
}

void *mem_alloc(size_t size) {
    if (pool_ptr + size > MEM_POOL_SIZE)
        mem_panic("mem_alloc: brak pamięci w puli");
    void *p = &pool[pool_ptr];
    pool_ptr += size;
    return p;
}

void *mem_alloc_aligned(size_t size, size_t align) {
    /* wyrównaj ptr do granicy align */
    size_t aligned = (pool_ptr + align - 1) & ~(align - 1);
    if (aligned + size > MEM_POOL_SIZE)
        mem_panic("mem_alloc_aligned: brak pamięci w puli");
    pool_ptr = aligned + size;
    return &pool[aligned];
}

void mem_reset(void) {
    pool_ptr = 0;
}

size_t mem_used(void)  { return pool_ptr; }
size_t mem_free(void)  { return MEM_POOL_SIZE - pool_ptr; }

void mem_panic(const char *msg) {
    /* Przywróć tryb tekstowy przed wyświetleniem błędu */
#ifdef DOS_BUILD
    __dpmi_regs r;
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
#endif
    fprintf(stderr, "\nBŁĄD KRYTYCZNY: %s\n", msg);
    fprintf(stderr, "Użyte: %u / %u bajtów\n",
            (unsigned)pool_ptr, (unsigned)MEM_POOL_SIZE);
    exit(1);
}
