/*
 * pcx.c — PCX loader for VGA Mode 13h (320×200, 256 colours)
 * Gruniożerca DOS port
 *
 * Supports DJGPP (DOS_BUILD) and Open Watcom (__WATCOMC__).
 *
 * PCX RLE encoding (per-scanline):
 *   - If top 2 bits of byte are 11 (byte >= 0xC0):
 *       count = byte & 0x3F  (1-63 repetitions)
 *       next byte = pixel value to repeat
 *   - Otherwise: byte itself is a single pixel value
 *
 * 256-colour palette block: last 769 bytes of file.
 *   Byte 0   : 0x0C (magic marker)
 *   Bytes 1-768 : 256 × (R, G, B) each 0-255
 *   VGA DAC uses 0-63, so shift right by 2.
 *
 * Scanline padding: bytes_per_line >= image width.  The decoder
 * always consumes bytes_per_line bytes per row but only writes the
 * first `width` pixels to the output buffer / VRAM.
 */
#include "pcx.h"
#include "pack.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
   Compiler-specific VGA / I/O helpers
   ----------------------------------------------------------------------- */

#if defined(DOS_BUILD) && defined(__DJGPP__)
/* ---- DJGPP ------------------------------------------------------------ */
#   include <go32.h>
#   include <dpmi.h>
#   include <sys/nearptr.h>
#   include <pc.h>          /* outportb */

    /* Enable near-pointer access if not already done.
       Returns pointer to the start of linear VGA memory. */
    static uint8_t *vga_ptr(void)
    {
        __djgpp_nearptr_enable();
        return (uint8_t *)(0xA0000uL + __djgpp_conventional_base);
    }

    static void set_mode13h(void)
    {
        __dpmi_regs r;
        memset(&r, 0, sizeof(r));
        r.x.ax = 0x0013;
        __dpmi_int(0x10, &r);
    }

    static void dac_write(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
    {
        outportb(0x3C8, idx);
        outportb(0x3C9, r);
        outportb(0x3C9, g);
        outportb(0x3C9, b);
    }

#elif defined(__WATCOMC__)
/* ---- Open Watcom (32-bit protected mode, DOS4G/W or similar) ---------- */
#   include <i86.h>
#   include <conio.h>

    static uint8_t *vga_ptr(void)
    {
        /* DOS4G/W maps conventional memory 1:1 at linear address 0xA0000 */
        return (uint8_t *)0xA0000uL;
    }

    static void set_mode13h(void)
    {
        union REGS regs;
        memset(&regs, 0, sizeof(regs));
        regs.w.ax = 0x0013;
        int386(0x10, &regs, &regs);
    }

    static void dac_write(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
    {
        outp(0x3C8, idx);
        outp(0x3C9, r);
        outp(0x3C9, g);
        outp(0x3C9, b);
    }

#else
/* ---- Host / non-DOS build: stubs so the translation unit compiles ----- */
    static uint8_t *vga_ptr(void)           { return NULL; }
    static void set_mode13h(void)           { }
    static void dac_write(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
                                            { (void)i;(void)r;(void)g;(void)b; }
#endif

/* -----------------------------------------------------------------------
   Constants
   ----------------------------------------------------------------------- */
#define PCX_WIDTH        320
#define PCX_HEIGHT       200
#define PCX_PIXELS       (PCX_WIDTH * PCX_HEIGHT)   /* 64 000               */
#define PCX_PAL_MARKER   0x0C                        /* byte before palette  */
#define PCX_PAL_SIZE     768                         /* 256 × 3 bytes        */
#define PCX_PAL_BLOCK    769                         /* marker + palette     */

/* -----------------------------------------------------------------------
   Internal: open + validate header
   ----------------------------------------------------------------------- */
static PCXResult pcx_open_validate(const char *path, FILE **fp_out,
                                   PCXHeader  *hdr_out,
                                   int *width_out, int *height_out)
{
    FILE     *fp;
    PCXHeader hdr;

    fp = fopen(path, "rb");
    if (!fp)
        return PCX_ERR_OPEN;

    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return PCX_ERR_FORMAT;
    }

    /* Validate: ZSoft magic, RLE encoding, 8 bpp, 1 plane */
    if (hdr.manufacturer != 0x0A ||
        hdr.encoding     != 1    ||
        hdr.bpp          != 8    ||
        hdr.color_planes != 1) {
        fclose(fp);
        return PCX_ERR_FORMAT;
    }

    int w = (int)hdr.x_max - (int)hdr.x_min + 1;
    int h = (int)hdr.y_max - (int)hdr.y_min + 1;

    if (w != PCX_WIDTH || h != PCX_HEIGHT) {
        fclose(fp);
        return PCX_ERR_SIZE;
    }

    *fp_out     = fp;
    *hdr_out    = hdr;
    *width_out  = w;
    *height_out = h;
    return PCX_OK;
}

/* -----------------------------------------------------------------------
   Internal: RLE decode into dst[]
   Decodes height scanlines of bytes_per_line bytes each.
   Only the first `width` bytes of each scanline are stored.
   ----------------------------------------------------------------------- */
static PCXResult pcx_decode(FILE *fp, uint8_t *dst,
                             int width, int height,
                             int bytes_per_line)
{
    int   row, col;
    int   b, run_count;
    uint8_t pixel;

    for (row = 0; row < height; row++) {
        col = 0;
        while (col < bytes_per_line) {
            b = fgetc(fp);
            if (b == EOF) return PCX_ERR_READ;

            if ((b & 0xC0) == 0xC0) {
                /* RLE run: top 2 bits set */
                run_count = b & 0x3F;
                b = fgetc(fp);
                if (b == EOF) return PCX_ERR_READ;
                pixel = (uint8_t)b;

                while (run_count-- > 0 && col < bytes_per_line) {
                    if (col < width)
                        dst[row * width + col] = pixel;
                    col++;
                }
            } else {
                /* Single literal pixel */
                if (col < width)
                    dst[row * width + col] = (uint8_t)b;
                col++;
            }
        }
    }
    return PCX_OK;
}

/* -----------------------------------------------------------------------
   Internal: read 768-byte palette block from end of file.
   Returns 1 on success, 0 if the palette marker is missing.
   ----------------------------------------------------------------------- */
static int pcx_read_palette(FILE *fp, uint8_t pal[PCX_PAL_SIZE])
{
    if (fseek(fp, -(long)PCX_PAL_BLOCK, SEEK_END) != 0)
        return 0;
    if (fgetc(fp) != PCX_PAL_MARKER)
        return 0;
    return (fread(pal, 1, PCX_PAL_SIZE, fp) == PCX_PAL_SIZE);
}

/* -----------------------------------------------------------------------
   Memory cursor — dekoder PCX z bufora RAM (do ładowania z archiwum)
   ----------------------------------------------------------------------- */
typedef struct { const uint8_t *data; uint32_t pos, size; } MCursor;

static int mc_getc(MCursor *mc) {
    return (mc->pos < mc->size) ? (int)mc->data[mc->pos++] : EOF;
}
static size_t mc_read(MCursor *mc, void *buf, size_t n) {
    size_t avail = mc->size - mc->pos;
    if (n > avail) n = avail;
    memcpy(buf, mc->data + mc->pos, n);
    mc->pos += (uint32_t)n;
    return n;
}

static PCXResult pcx_decode_mc(MCursor *mc, uint8_t *dst,
                                int width, int height, int bytes_per_line) {
    int row, col, b, run_count;
    uint8_t pixel;
    for (row = 0; row < height; row++) {
        col = 0;
        while (col < bytes_per_line) {
            b = mc_getc(mc);
            if (b == EOF) return PCX_ERR_READ;
            if ((b & 0xC0) == 0xC0) {
                run_count = b & 0x3F;
                b = mc_getc(mc);
                if (b == EOF) return PCX_ERR_READ;
                pixel = (uint8_t)b;
                while (run_count-- > 0 && col < bytes_per_line) {
                    if (col < width) dst[row * width + col] = pixel;
                    col++;
                }
            } else {
                if (col < width) dst[row * width + col] = (uint8_t)b;
                col++;
            }
        }
    }
    return PCX_OK;
}

/* Dekoduje PCX z bufora RAM; używane gdy plik pochodzi z archiwum GRUNIO.DAT */
static PCXResult pcx_load_buf_mem(const uint8_t *src, uint32_t src_size,
                                   uint8_t *buf, size_t buf_size,
                                   uint8_t *pal_out) {
    PCXHeader hdr;
    MCursor mc;
    int w, h;
    PCXResult rc;

    if (!buf || buf_size < PCX_PIXELS || !src || src_size < sizeof(PCXHeader))
        return PCX_ERR_SIZE;

    mc.data = src; mc.pos = 0; mc.size = src_size;
    if (mc_read(&mc, &hdr, sizeof(hdr)) != sizeof(hdr)) return PCX_ERR_FORMAT;

    if (hdr.manufacturer != 0x0A || hdr.encoding != 1 ||
        hdr.bpp != 8 || hdr.color_planes != 1)
        return PCX_ERR_FORMAT;

    w = (int)(hdr.x_max - hdr.x_min + 1);
    h = (int)(hdr.y_max - hdr.y_min + 1);
    if (w != PCX_WIDTH || h != PCX_HEIGHT) return PCX_ERR_SIZE;

    rc = pcx_decode_mc(&mc, buf, w, h, (int)hdr.bytes_per_line);

    if (rc == PCX_OK && pal_out && src_size >= PCX_PAL_BLOCK) {
        mc.pos = src_size - PCX_PAL_BLOCK;
        if (mc_getc(&mc) == PCX_PAL_MARKER)
            mc_read(&mc, pal_out, PCX_PAL_SIZE);
    }
    return rc;
}

/* -----------------------------------------------------------------------
   pcx_apply_palette — program VGA DAC
   PCX stores 0-255; VGA DAC accepts 0-63  →  >> 2
   ----------------------------------------------------------------------- */
void pcx_apply_palette(const uint8_t *pal)
{
    int i;
    for (i = 0; i < 256; i++)
        dac_write((uint8_t)i,
                  pal[i * 3 + 0] >> 2,   /* R */
                  pal[i * 3 + 1] >> 2,   /* G */
                  pal[i * 3 + 2] >> 2);  /* B */
}

/* -----------------------------------------------------------------------
   pcx_display — Mode 13h + direct VRAM write + palette
   ----------------------------------------------------------------------- */
PCXResult pcx_display(const char *path)
{
    FILE      *fp;
    PCXHeader  hdr;
    int        width, height;
    PCXResult  rc;
    uint8_t    pal[PCX_PAL_SIZE];
    uint8_t   *vga;

    rc = pcx_open_validate(path, &fp, &hdr, &width, &height);
    if (rc != PCX_OK) return rc;

    /* Switch to Mode 13h before touching VRAM */
    set_mode13h();

    vga = vga_ptr();

    if (vga) {
        rc = pcx_decode(fp, vga,
                        width, height,
                        (int)hdr.bytes_per_line);
    } else {
        rc = PCX_ERR_READ;   /* non-DOS build: vga_ptr() returned NULL */
    }

    if (rc == PCX_OK && pcx_read_palette(fp, pal))
        pcx_apply_palette(pal);

    fclose(fp);
    return rc;
}

/* -----------------------------------------------------------------------
   pcx_load_backbuf — decode into video_backbuf[] (see video.h)
   ----------------------------------------------------------------------- */
#include "video.h"   /* for video_backbuf and pcx_apply_palette DAC call  */

PCXResult pcx_load_backbuf(const char *path)
{
    FILE      *fp;
    PCXHeader  hdr;
    int        width, height;
    PCXResult  rc;
    uint8_t    pal[PCX_PAL_SIZE];

    rc = pcx_open_validate(path, &fp, &hdr, &width, &height);
    if (rc != PCX_OK) return rc;

    rc = pcx_decode(fp, video_backbuf,
                    width, height,
                    (int)hdr.bytes_per_line);

    if (rc == PCX_OK && pcx_read_palette(fp, pal))
        pcx_apply_palette(pal);

    fclose(fp);
    return rc;
}

/* -----------------------------------------------------------------------
   pcx_load_buf — decode into caller-supplied buffer (no VGA writes)
   ----------------------------------------------------------------------- */
PCXResult pcx_load_buf(const char *path,
                        uint8_t   *buf,
                        size_t     buf_size,
                        uint8_t   *pal_out)
{
    FILE      *fp;
    PCXHeader  hdr;
    int        width, height;
    PCXResult  rc;

    if (!buf || buf_size < PCX_PIXELS)
        return PCX_ERR_SIZE;

    /* Próbuj najpierw z archiwum GRUNIO.DAT */
    {
        uint32_t psz = 0;
        const uint8_t *pdata = pack_get_path(path, &psz);
        if (pdata)
            return pcx_load_buf_mem(pdata, psz, buf, buf_size, pal_out);
    }

    /* Fallback: plik na dysku */
    rc = pcx_open_validate(path, &fp, &hdr, &width, &height);
    if (rc != PCX_OK) return rc;

    rc = pcx_decode(fp, buf, width, height, (int)hdr.bytes_per_line);
    if (rc == PCX_OK && pal_out)
        pcx_read_palette(fp, pal_out);

    fclose(fp);
    return rc;
}
