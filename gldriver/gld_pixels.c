/* gld_pixels.c - glReadPixels / glDrawPixels / glCopyPixels / glBitmap through the renderer hook slots (ctl08 / 0c / 10 / 14).
 * The bundle never holds a CPU view of VRAM (design 4): every pixel op moves whole target rows through the kernel (rgb_readback2 / rgb_upload2,
 * RGB565 with the SWAP16 transfer so a uint16_t array holds the logical pixel values), edits them in a malloc'd buffer, and writes the rows back.
 * Slot protocols and GL_PACK/GL_UNPACK offsets: docs/re-notes/readpixels.md and drawpixels.md (hardware-verified against GLEngine). */
#include <stdlib.h>
#include <string.h>
#include "gld.h"

#define XF16 1u                                   /* RAGE_XFER_SWAP16 */

static int iceil(float f) { int i = (int)f; return (f > (float)i) ? i + 1 : i; }
static uint32_t bswap32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24); }

/* GL_PACK_* / GL_UNPACK_* state in GLEngine's gc (offsets measured live; UNPACK = PACK + 0x1c) */
static void state_at(const unsigned char *g, unsigned o_align, unsigned o_rowlen, unsigned o_skippix, unsigned o_skiprows, unsigned o_swap, unsigned o_lsb,
                     int *align, int *rowlen, int *skippix, int *skiprows, int *swap, int *lsb)
{
    int a;
    *align = 4; *rowlen = 0; *skippix = 0; *skiprows = 0; *swap = 0; *lsb = 0;
    if (!g) return;
    a = *(const int *)(g + o_align); if (a == 1 || a == 2 || a == 4 || a == 8) *align = a;
    *rowlen = *(const int *)(g + o_rowlen); *skippix = *(const int *)(g + o_skippix); *skiprows = *(const int *)(g + o_skiprows);
    *swap = g[o_swap] ? 1 : 0; *lsb = o_lsb ? (g[o_lsb] ? 1 : 0) : 0;
    if (*rowlen < 0) *rowlen = 0; if (*skippix < 0) *skippix = 0; if (*skiprows < 0) *skiprows = 0;
}

/* a window of framebuffer rows held in memory: fb rows [fr0, fr0+nr), each pitch px of uint16 */
typedef struct { uint16_t *buf; int fr0, nr; uint32_t pitch; uint32_t off; } pxwin;

static int win_get(mach64_priv *p, uint32_t off, int fr0, int nr, pxwin *w)
{
    uint32_t pb = p->rb.pitch * 2u, done = 0, total, chunk; int rows;
    w->buf = 0; w->fr0 = fr0; w->nr = nr; w->pitch = p->rb.pitch; w->off = off;
    if (nr <= 0) return 0;
    total = pb * (uint32_t)nr;
    w->buf = (uint16_t *)malloc(total);
    if (!w->buf) return -1;
    rows = (int)(0x100000u / pb); if (rows < 1) rows = 1;
    chunk = pb * (uint32_t)rows;
    while (done < total) {
        uint32_t n = total - done < chunk ? total - done : chunk;
        if (rgb_readback2(&p->rb, off + (uint32_t)fr0 * pb + done, (unsigned char *)w->buf + done, n, XF16)) { free(w->buf); w->buf = 0; return -1; }
        done += n;
    }
    return 0;
}
static int win_put(mach64_priv *p, pxwin *w)
{
    uint32_t pb = p->rb.pitch * 2u, done = 0, total = pb * (uint32_t)w->nr, chunk; int rows, rc = 0;
    if (!w->buf) return 0;
    rows = (int)(0x100000u / pb); if (rows < 1) rows = 1;
    chunk = pb * (uint32_t)rows;
    while (done < total) {
        uint32_t n = total - done < chunk ? total - done : chunk;
        if (rgb_upload2(&p->rb, w->off + (uint32_t)w->fr0 * pb + done, (unsigned char *)w->buf + done, n, XF16)) { rc = -1; break; }
        done += n;
    }
    free(w->buf); w->buf = 0;
    return rc;
}
/* pixel (x, GL row y) of a window; 0 if outside */
static uint16_t *win_px(pxwin *w, int H, int x, int gy, int W)
{
    int fr = H - 1 - gy;
    if (x < 0 || x >= W || fr < w->fr0 || fr >= w->fr0 + w->nr) return 0;
    return w->buf + (uint32_t)(fr - w->fr0) * w->pitch + (uint32_t)x;
}
static int win_rows_for(int H, int ylo, int yhi, int *fr0, int *nr)   /* GL rows [ylo,yhi) -> clipped fb rows */
{
    int a = H - yhi, b = H - 1 - ylo;
    if (a < 0) a = 0; if (b > H - 1) b = H - 1;
    if (b < a) { *fr0 = 0; *nr = 0; return 0; }
    *fr0 = a; *nr = b - a + 1; return 1;
}

static int fmt_decode(unsigned format, unsigned type, int *is_bgr, int *has_a, int *pk)
{
    switch (format) {
        case 0x1908u: *is_bgr = 0; *has_a = 1; break;
        case 0x80E1u: *is_bgr = 1; *has_a = 1; break;
        case 0x1907u: *is_bgr = 0; *has_a = 0; break;
        case 0x80E0u: *is_bgr = 1; *has_a = 0; break;
        default: return 0;
    }
    switch (type) {
        case 0x1401u: *pk = 0; break;
        case 0x8035u: *pk = 1; if (!*has_a) return 0; break;
        case 0x8367u: *pk = 2; if (!*has_a) return 0; break;
        default: return 0;
    }
    return 1;
}
static uint16_t p565(unsigned R, unsigned G, unsigned B) { return (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)); }

/* glReadPixels: (rb_ctx = our gld_context, x, y, w, h, format, type, pixels) */
int mach64_ctl08(void *rb_ctx, int x, int y, int w, int h, unsigned format, unsigned type, void *pixels)
{
    gld_context *c = (gld_context *)rb_ctx; mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    int W, H, fr0, nr, p_align, p_rowlen, p_skippix, p_skiprows, p_swap, p_lsb, l, row_stride, row, col, is_depth = (format == 0x1902u);
    pxwin win; unsigned char *base = (unsigned char *)pixels;
    if (!p || !pixels || w <= 0 || h <= 0 || !mach64_mode_check(c) || !p->rb.col_off) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h;
    state_at(p->gc, 0x63d8, 0x63c4, 0x63d0, 0x63cc, 0x63dc, 0, &p_align, &p_rowlen, &p_skippix, &p_skiprows, &p_swap, &p_lsb);
    l = p_rowlen > 0 ? p_rowlen : w;
    /* no frame_start here: after a present frame_new is set, and a readback must not run the next frame's deferred clear over the picture */
    mach64_flush_batch(c);
    if (!win_rows_for(H, y, y + h, &fr0, &nr)) return 0;
    if (is_depth) {
        int elem;
        if (!p->rb.z_off) return 0;
        switch (type) { case 0x1403u: elem = 2; break; case 0x1405u: case 0x1406u: elem = 4; break; default: return 0; }
        if (win_get(p, p->rb.z_off, fr0, nr, &win)) return 0;
        row_stride = (elem >= p_align) ? l * elem : p_align * ((l * elem + p_align - 1) / p_align);
        base += p_skiprows * row_stride + p_skippix * elem;
        for (row = 0; row < h; row++) {
            unsigned char *o = base + row * row_stride;
            for (col = 0; col < w; col++, o += elem) {
                uint16_t *s = win_px(&win, H, x + col, y + row, W);
                unsigned z16;
                if (!s) continue;
                z16 = *s;
                if (type == 0x1403u) { uint16_t v = (uint16_t)z16; if (p_swap) v = (uint16_t)((v >> 8) | (v << 8)); *(uint16_t *)o = v; }
                else { union { float f; uint32_t u; } out;
                       if (type == 0x1405u) out.u = (z16 << 16) | z16; else out.f = (float)z16 / 65535.0f;
                       if (p_swap) out.u = bswap32(out.u);
                       *(uint32_t *)o = out.u; }
            }
        }
        free(win.buf);
        return 0;
    }
    {   int is_bgr, has_a, pk, bpp_out, elem;
        if (!fmt_decode(format, type, &is_bgr, &has_a, &pk)) return 0;
        bpp_out = (pk || has_a) ? 4 : 3; elem = pk ? 4 : 1;
        if (win_get(p, p->rb.col_off, fr0, nr, &win)) return 0;
        row_stride = (elem >= p_align) ? l * bpp_out : p_align * ((l * bpp_out + p_align - 1) / p_align);
        base += p_skiprows * row_stride + p_skippix * bpp_out;
        for (row = 0; row < h; row++) {
            unsigned char *o = base + row * row_stride;
            for (col = 0; col < w; col++) {
                uint16_t *s = win_px(&win, H, x + col, y + row, W);
                unsigned R = 0, G = 0, B = 0, A = 0xffu;
                if (s) { unsigned v = *s, r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31;
                         R = (r5 << 3) | (r5 >> 2); G = (g6 << 2) | (g6 >> 4); B = (b5 << 3) | (b5 >> 2); }
                if (pk == 0) {
                    if (is_bgr) { o[0] = (unsigned char)B; o[1] = (unsigned char)G; o[2] = (unsigned char)R; }
                    else        { o[0] = (unsigned char)R; o[1] = (unsigned char)G; o[2] = (unsigned char)B; }
                    if (has_a) o[3] = (unsigned char)A;
                    o += bpp_out;
                } else {
                    unsigned c0, c1, c2, c3, v;
                    if (is_bgr) { c0 = B; c1 = G; c2 = R; c3 = A; } else { c0 = R; c1 = G; c2 = B; c3 = A; }
                    v = (pk == 1) ? ((c0 << 24) | (c1 << 16) | (c2 << 8) | c3) : (c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
                    if (p_swap) v = bswap32(v);
                    *(uint32_t *)o = v; o += 4;
                }
            }
        }
        free(win.buf);
    }
    return 0;
}

static void decode_pixel(const unsigned char *s, int pk, int is_bgr, int has_a, int u_swap, unsigned char *R, unsigned char *G, unsigned char *B)
{
    (void)has_a;
    if (pk == 0) { if (is_bgr) { *B = s[0]; *G = s[1]; *R = s[2]; } else { *R = s[0]; *G = s[1]; *B = s[2]; } }
    else {
        uint32_t v = *(const uint32_t *)s, c0, c1, c2;
        if (u_swap) v = bswap32(v);
        if (pk == 1) { c0 = (v >> 24) & 0xffu; c1 = (v >> 16) & 0xffu; c2 = (v >> 8) & 0xffu; }
        else         { c0 = v & 0xffu; c1 = (v >> 8) & 0xffu; c2 = (v >> 16) & 0xffu; }
        if (is_bgr) { *B = (unsigned char)c0; *G = (unsigned char)c1; *R = (unsigned char)c2; } else { *R = (unsigned char)c0; *G = (unsigned char)c1; *B = (unsigned char)c2; }
    }
}

/* glDrawPixels: (raster_dst float*{x,y,z}, width, height, format, type, pixels) */
int mach64_ctl0c(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    const float *rp = (const float *)(uintptr_t)a; const unsigned char *src = (const unsigned char *)(uintptr_t)g;
    int w = (int)b, h = (int)d, is_bgr, has_a, pk, bpp_in, elem, dx, dy, W, H, row, col, align, rowstride, u_rowlen, u_skippix, u_skiprows, u_swap, u_lsb, l;
    int fr0, nr, ylo_all, yhi_all; float zx = 1.0f, zy = 1.0f; pxwin win;
    if (!p || !p->rb.col_off || !rp || !src || w <= 0 || h <= 0 || !fmt_decode(e, f, &is_bgr, &has_a, &pk)) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h;
    bpp_in = (pk || has_a) ? 4 : 3; elem = pk ? 4 : 1;
    dx = (int)(rp[0] + 0.5f); dy = (int)(rp[1] + 0.5f);
    state_at(p->gc, 0x63f4, 0x63e0, 0x63ec, 0x63e8, 0x63f8, 0x63f9, &align, &u_rowlen, &u_skippix, &u_skiprows, &u_swap, &u_lsb);
    if (p->gc) { float rzx = *(const float *)(p->gc + 0x63bc), rzy = *(const float *)(p->gc + 0x63c0); if (rzx != 0.0f) zx = rzx; if (rzy != 0.0f) zy = rzy; }
    l = u_rowlen > 0 ? u_rowlen : w;
    rowstride = (elem >= align) ? l * bpp_in : align * ((l * bpp_in + align - 1) / align);
    src += u_skiprows * rowstride + u_skippix * bpp_in;
    mach64_frame_start(c); mach64_flush_batch(c);
    { float y0 = (float)dy, y1 = (float)dy + (float)h * zy;
      ylo_all = iceil((y0 < y1 ? y0 : y1) - 0.5f); yhi_all = iceil((y0 < y1 ? y1 : y0) - 0.5f); }
    if (!win_rows_for(H, ylo_all, yhi_all, &fr0, &nr)) return 0;
    if (win_get(p, p->rb.col_off, fr0, nr, &win)) return 0;
    for (row = 0; row < h; row++) {
        const unsigned char *srow = src + row * rowstride;
        float wy0 = (float)dy + (float)row * zy, wy1 = (float)dy + (float)(row + 1) * zy;
        int ylo = iceil((wy0 < wy1 ? wy0 : wy1) - 0.5f), yhi = iceil((wy0 < wy1 ? wy1 : wy0) - 0.5f), wy;
        for (col = 0; col < w; col++) {
            float wx0 = (float)dx + (float)col * zx, wx1 = (float)dx + (float)(col + 1) * zx;
            int xlo = iceil((wx0 < wx1 ? wx0 : wx1) - 0.5f), xhi = iceil((wx0 < wx1 ? wx1 : wx0) - 0.5f), wx;
            unsigned char R, G, B; uint16_t v;
            decode_pixel(srow + col * bpp_in, pk, is_bgr, has_a, u_swap, &R, &G, &B);
            v = p565(R, G, B);
            for (wy = ylo; wy < yhi; wy++) for (wx = xlo; wx < xhi; wx++) { uint16_t *o = win_px(&win, H, wx, wy, W); if (o) *o = v; }
        }
    }
    (void)u_lsb;
    win_put(p, &win);
    return 0;
}

/* glCopyPixels(GL_COLOR): (raster_dst float*, src_x, src_y, w, h, type).  The source rows are read into memory first, so overlapping rects are correct. */
int mach64_ctl10(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    const float *rp = (const float *)(uintptr_t)a;
    int sx = (int)b, sy = (int)d, w = (int)e, h = (int)f, dx, dy, W, H, row, col, sfr0, snr, dfr0, dnr; pxwin src, dst;
    (void)g;
    if (!p || !p->rb.col_off || !rp || w <= 0 || h <= 0) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h;
    dx = (int)(rp[0] + 0.5f); dy = (int)(rp[1] + 0.5f);
    mach64_frame_start(c); mach64_flush_batch(c);
    if (!win_rows_for(H, sy, sy + h, &sfr0, &snr) || !win_rows_for(H, dy, dy + h, &dfr0, &dnr)) return 0;
    if (win_get(p, p->rb.col_off, sfr0, snr, &src)) return 0;
    if (win_get(p, p->rb.col_off, dfr0, dnr, &dst)) { free(src.buf); return 0; }
    for (row = 0; row < h; row++) for (col = 0; col < w; col++) {
        uint16_t *s = win_px(&src, H, sx + col, sy + row, W), *o = win_px(&dst, H, dx + col, dy + row, W);
        if (s && o) *o = *s;
    }
    free(src.buf);
    win_put(p, &dst);
    return 0;
}

/* glBitmap: (raster_dst float*, width, height, _, _, bitmap): set bits drawn in the current raster colour (gc+0x2e4, RGBA floats) */
int mach64_ctl14(gld_context *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    const float *rp = (const float *)(uintptr_t)a; const unsigned char *bm = (const unsigned char *)(uintptr_t)g;
    int w = (int)b, h = (int)d, dx, dy, W, H, row, col, rowbytes, u_align, u_rowlen, u_skippix, u_skiprows, u_swap, u_lsb, lbits, fr0, nr; pxwin win; uint16_t v;
    (void)e; (void)f;
    if (!p || !p->rb.col_off || !rp || !bm || w <= 0 || h <= 0) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h;
    if (p->gc) { const float *rc = (const float *)(p->gc + 0x2e4);
        v = p565((unsigned)rage_f2byte(gld_bits(rc[0])), (unsigned)rage_f2byte(gld_bits(rc[1])), (unsigned)rage_f2byte(gld_bits(rc[2]))); }
    else v = 0xffffu;
    dx = (int)(rp[0] + 0.5f); dy = (int)(rp[1] + 0.5f);
    state_at(p->gc, 0x63f4, 0x63e0, 0x63ec, 0x63e8, 0x63f8, 0x63f9, &u_align, &u_rowlen, &u_skippix, &u_skiprows, &u_swap, &u_lsb);
    lbits = u_rowlen > 0 ? u_rowlen : w;
    rowbytes = (((lbits + 7) / 8 + u_align - 1) / u_align) * u_align;
    bm += u_skiprows * rowbytes;
    mach64_frame_start(c); mach64_flush_batch(c);
    if (!win_rows_for(H, dy, dy + h, &fr0, &nr)) return 0;
    if (win_get(p, p->rb.col_off, fr0, nr, &win)) return 0;
    for (row = 0; row < h; row++) {
        const unsigned char *brow = bm + row * rowbytes;
        for (col = 0; col < w; col++) {
            int bitidx = u_skippix + col, bpos = bitidx & 7; unsigned char mask = u_lsb ? (unsigned char)(1u << bpos) : (unsigned char)(0x80u >> bpos);
            uint16_t *o;
            if (!(brow[bitidx >> 3] & mask)) continue;
            o = win_px(&win, H, dx + col, dy + row, W); if (o) *o = v;
        }
    }
    win_put(p, &win);
    return 0;
}
