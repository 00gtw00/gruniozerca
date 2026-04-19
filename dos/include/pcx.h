/*
 * pcx.h — PCX loader for VGA Mode 13h (320×200, 256 colours)
 * Gruniożerca DOS port
 *
 * Compatible with DJGPP (DOS_BUILD) and Open Watcom (__WATCOMC__).
 *
 * Two modes of operation:
 *
 *   pcx_display(path)
 *       Sets Mode 13h, decodes RLE directly into VRAM (0xA000:0),
 *       and applies the embedded palette.  Stand-alone — does not
 *       require video_init().
 *
 *   pcx_load_backbuf(path)
 *       Decodes into video_backbuf[] (from video.h) and applies the
 *       palette.  Use with video_flip() inside the normal render loop.
 *
 *   pcx_load_buf(path, buf, buf_size, pal_out)
 *       Low-level: decode into any 64 000-byte buffer; optionally
 *       return the raw 768-byte palette.  Does not touch VGA hardware.
 */
#ifndef PCX_H
#define PCX_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
   PCX file header — 128 bytes, packed
   ----------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  manufacturer;      /* 0x0A = ZSoft PCX                     */
    uint8_t  version;           /* 5 = PCX v3.0 (256-colour)            */
    uint8_t  encoding;          /* 1 = RLE                               */
    uint8_t  bpp;               /* 8 for 256-colour single-plane         */
    uint16_t x_min, y_min;      /* image window — origin (usually 0,0)  */
    uint16_t x_max, y_max;      /* inclusive: width = x_max-x_min+1     */
    uint16_t h_dpi, v_dpi;      /* screen DPI (informational)           */
    uint8_t  ega_palette[48];   /* EGA palette — unused for 256-colour  */
    uint8_t  reserved;          /* must be 0                             */
    uint8_t  color_planes;      /* 1 for 256-colour                      */
    uint16_t bytes_per_line;    /* bytes per decoded scanline (≥ width)  */
    uint16_t palette_type;      /* 1=colour, 2=grey                      */
    uint16_t h_screen;          /* source screen width  (informational)  */
    uint16_t v_screen;          /* source screen height (informational)  */
    uint8_t  padding[54];       /* reserved, zero-padded                 */
} PCXHeader;                    /* total: 128 bytes                      */
#pragma pack(pop)

/* -----------------------------------------------------------------------
   Return codes
   ----------------------------------------------------------------------- */
typedef enum {
    PCX_OK          = 0,
    PCX_ERR_OPEN    = 1,   /* file not found / cannot open  */
    PCX_ERR_FORMAT  = 2,   /* not a valid 256-colour PCX    */
    PCX_ERR_SIZE    = 3,   /* image is not 320 × 200        */
    PCX_ERR_READ    = 4    /* unexpected end of file        */
} PCXResult;

/* -----------------------------------------------------------------------
   API
   ----------------------------------------------------------------------- */

/*
 * pcx_display — all-in-one: Mode 13h + VRAM write + palette.
 *
 * Sets the VGA adapter to Mode 13h (INT 10h, AX=0013h), decodes the
 * RLE stream byte-by-byte into linear video memory at 0xA000:0, then
 * reads the 768-byte palette block from the end of the file and
 * programs VGA DAC registers via ports 0x3C8 / 0x3C9.
 *
 * Does not require video_init() to have been called.
 */
PCXResult pcx_display(const char *path);

/*
 * pcx_load_backbuf — decode into video_backbuf[].
 *
 * Requires video_init() to have been called (so that Mode 13h is
 * active and the back buffer exists).  Call video_flip() afterwards
 * to push the image to VRAM.  The VGA palette is programmed
 * immediately so colours are correct on the next flip.
 */
PCXResult pcx_load_backbuf(const char *path);

/*
 * pcx_load_buf — low-level decode into any byte buffer.
 *
 *   buf      : destination buffer; must be at least 320*200 = 64 000 bytes.
 *   buf_size : size of buf in bytes (checked; returns PCX_ERR_SIZE if < 64000).
 *   pal_out  : if non-NULL, receives the raw 768-byte RGB palette
 *              (values 0–255; divide by 4 for VGA DAC).
 *
 * Does not touch VGA hardware.
 */
PCXResult pcx_load_buf(const char *path,
                        uint8_t   *buf,
                        size_t     buf_size,
                        uint8_t   *pal_out);

/*
 * pcx_apply_palette — program VGA DAC from a 768-byte PCX palette.
 *
 *   pal : 256 × (R, G, B) bytes, values 0–255.
 *
 * Converts PCX 0–255 range to VGA DAC 0–63 by right-shifting 2 bits.
 * Writes all 256 entries starting at DAC index 0.
 */
void pcx_apply_palette(const uint8_t *pal);

#endif /* PCX_H */
