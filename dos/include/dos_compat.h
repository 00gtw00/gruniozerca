/*
 * dos_compat.h — Makra zgodności DJGPP / DOS
 * Gruniożerca DOS port
 *
 * Dostarcza:
 *  - inportb/outportb (przez <pc.h> DJGPP)
 *  - dostęp do pamięci konwencjonalnej (near pointer)
 *  - makra cli/sti
 *  - DPMI interrupt wrappers
 */
#ifndef DOS_COMPAT_H
#define DOS_COMPAT_H

#ifdef DOS_BUILD

#include <stdint.h>
#include <pc.h>           /* inportb, outportb, inportw, outportw */
#include <dpmi.h>         /* __dpmi_regs, __dpmi_int */
#include <go32.h>         /* _go32_dpmi_seginfo, _go32_my_cs() */
#include <sys/nearptr.h>  /* __djgpp_nearptr_enable, __djgpp_conventional_base */
#include <sys/farptr.h>   /* _farpokeb, _farpeekb, _farpokew itd. */
#include <dos.h>          /* union REGS, int86 */

/* Wyłącz/włącz przerwania — krytyczne sekcje */
#define CLI()   __asm__ volatile("cli")
#define STI()   __asm__ volatile("sti")

/* Selektor pamięci konwencjonalnej DOS (0–1 MB) */
#define DOS_MEM_SEL  (_dos_ds)

/* Przelicz adres fizyczny konwencjonalny → wskaźnik near DJGPP */
#define PHYS_TO_NEAR(addr) ((void*)((uint32_t)(addr) + __djgpp_conventional_base))

/* Zapis/odczyt bajtu pod adresem fizycznym (wolniejszy, ale zawsze bezpieczny) */
#define POKE8(phys, val)   _farpokeb(DOS_MEM_SEL, (uint32_t)(phys), (uint8_t)(val))
#define PEEK8(phys)        _farpeekb(DOS_MEM_SEL, (uint32_t)(phys))
#define POKE16(phys, val)  _farpokew(DOS_MEM_SEL, (uint32_t)(phys), (uint16_t)(val))
#define PEEK16(phys)       _farpeekw(DOS_MEM_SEL, (uint32_t)(phys))

/* Wywołanie przerwania BIOS przez DPMI */
static inline void bios_int(int vector, __dpmi_regs *regs) {
    __dpmi_int(vector, regs);
}

#else /* kompilacja na hoście (narzędzia) */

#include <stdint.h>
#define CLI()   ((void)0)
#define STI()   ((void)0)
static inline uint8_t  inportb(uint16_t p)            { (void)p; return 0; }
static inline void     outportb(uint16_t p, uint8_t v) { (void)p; (void)v; }

#endif /* DOS_BUILD */

/* ---------- Wspólne makra (host i DOS) ---------- */

/* Fixed-point 8.8 — prędkość i pozycja */
typedef int32_t  fp8_t;   /* 8.8 ze znakiem — int32 by uniknąć overflow przy x>127 */
typedef uint32_t ufp8_t;  /* 8.8 bez znaku   */
#define FP8(int_part)  ((fp8_t)((int32_t)(int_part) * 256))
#define FP8_INT(fp)    ((int)((fp) >> 8))
#define FP8_FRAC(fp)   ((fp) & 0xFF)

/* Barwy-indeksy logiczne (muszą pasować do palette.dat) */
#define COL_TRANSPARENT  0
#define COL_BLACK        1
#define COL_WHITE        2

/* Ograniczenia ekranu VGA Mode 13h */
#define SCREEN_W  320
#define SCREEN_H  200
#define SCREEN_BPP 1       /* bajty na piksel */

#endif /* DOS_COMPAT_H */
