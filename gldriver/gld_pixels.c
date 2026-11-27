/* gld_pixels.c - glReadPixels / glDrawPixels / glCopyPixels / glBitmap through the renderer hook slots (ctl08 / 0c / 10 / 14).
 * The bundle never holds a CPU view of VRAM (design 4): every pixel op moves whole target rows through the kernel (rgb_readback2 / rgb_upload2,
 * RGB565 with the SWAP16 transfer so a uint16_t array holds the logical pixel values), edits them in a malloc'd buffer, and writes the rows back.
 * Slot protocols and GL_PACK/GL_UNPACK offsets: docs/re-notes/readpixels.md and drawpixels.md (hardware-verified against GLEngine). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gld.h"

#define XFER_FOR(bpp) ((bpp) == 4u ? 2u : 1u)   /* RAGE_XFER_SWAP32 (ARGB8888) / RAGE_XFER_SWAP16: the buffer then holds logical pixel values */

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

#ifndef ACCUM_CLEAR_OFF
#define ACCUM_CLEAR_OFF 0x6310   /* gli offset of the glClearAccum colour (floats r,g,b,a) -- set from the gli diff */
#endif
/* a window of framebuffer rows held in memory: fb rows [fr0, fr0+nr), each pitch px of uint16 */
typedef struct { uint16_t *buf; int fr0, nr; uint32_t pitch; uint32_t off; uint32_t bpp; } pxwin;   /* buf is bytes of 2 or 4 byte pixels (bpp) */

static int win_get_bpp(mach64_priv *p, uint32_t off, int fr0, int nr, pxwin *w, uint32_t bpp)
{
    uint32_t pb = p->rb.pitch * bpp, done = 0, total, chunk; int rows;
    w->buf = 0; w->fr0 = fr0; w->nr = nr; w->pitch = p->rb.pitch; w->off = off; w->bpp = bpp;
    if (nr <= 0) return 0;
    total = pb * (uint32_t)nr;
    w->buf = (uint16_t *)malloc(total);
    if (!w->buf) return -1;
    rows = (int)(0x100000u / pb); if (rows < 1) rows = 1;
    chunk = pb * (uint32_t)rows;
    while (done < total) {
        uint32_t n = total - done < chunk ? total - done : chunk;
        if (rgb_readback2(&p->rb, off + (uint32_t)fr0 * pb + done, (unsigned char *)w->buf + done, n, XFER_FOR(bpp))) { free(w->buf); w->buf = 0; return -1; }
        done += n;
    }
    return 0;
}
static int win_get(mach64_priv *p, uint32_t off, int fr0, int nr, pxwin *w) { return win_get_bpp(p, off, fr0, nr, w, p->rb.bpp); }
static int win_put(mach64_priv *p, pxwin *w)
{
    uint32_t pb = p->rb.pitch * w->bpp, done = 0, total = pb * (uint32_t)w->nr, chunk; int rows, rc = 0;
    if (!w->buf) return 0;
    rows = (int)(0x100000u / pb); if (rows < 1) rows = 1;
    chunk = pb * (uint32_t)rows;
    while (done < total) {
        uint32_t n = total - done < chunk ? total - done : chunk;
        if (rgb_upload2(&p->rb, w->off + (uint32_t)w->fr0 * pb + done, (unsigned char *)w->buf + done, n, XFER_FOR(w->bpp))) { rc = -1; break; }
        done += n;
    }
    free(w->buf); w->buf = 0;
    return rc;
}
/* pixel (x, GL row y) of a window; 0 if outside */
static unsigned char *win_px(pxwin *w, int H, int x, int gy, int W)
{
    int fr = H - 1 - gy;
    if (x < 0 || x >= W || fr < w->fr0 || fr >= w->fr0 + w->nr) return 0;
    return (unsigned char *)w->buf + ((uint32_t)(fr - w->fr0) * w->pitch + (uint32_t)x) * w->bpp;
}
static inline uint32_t px_ld(const pxwin *w, const unsigned char *q) { return w->bpp == 4u ? *(const uint32_t *)(const void *)q : *(const uint16_t *)(const void *)q; }
static inline void px_st(const pxwin *w, unsigned char *q, uint32_t v) { if (w->bpp == 4u) *(uint32_t *)(void *)q = v; else *(uint16_t *)(void *)q = (uint16_t)v; }
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
/* the colour target is RGB565, or 1555 when the scanout is 15 bit (rb.fmt555) */
static uint32_t pack16(const mach64_priv *p, unsigned R, unsigned G, unsigned B)
{ if (p->rb.bpp == 4u) return 0xff000000u | (R << 16) | (G << 8) | B;       /* ARGB8888, alpha 1 */
  return p->rb.fmt555 ? (uint16_t)(((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3)) : (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)); }
static void unpack16(const mach64_priv *p, unsigned v, unsigned *R, unsigned *G, unsigned *B)
{
    if (p->rb.bpp == 4u) { *R = (v >> 16) & 255u; *G = (v >> 8) & 255u; *B = v & 255u; }
    else if (p->rb.fmt555) { unsigned r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31; *R = (r5 << 3) | (r5 >> 2); *G = (g5 << 3) | (g5 >> 2); *B = (b5 << 3) | (b5 >> 2); }
    else { unsigned r5 = (v >> 11) & 31, g6 = (v >> 5) & 63, b5 = v & 31; *R = (r5 << 3) | (r5 >> 2); *G = (g6 << 2) | (g6 >> 4); *B = (b5 << 3) | (b5 >> 2); }
}

/* glReadPixels: (rb_ctx = our gld_context, x, y, w, h, format, type, pixels) */
/* Pixel transfer (glPixelTransfer scale/bias, the colour matrix).  GLEngine keeps the state in the gli (static analysis of glPixelTransferf, GLEngine 0x29830..0x298f0, and
 * glMatrixMode 0x22088 / 0x94aa8): scales R,G,B,A at +0x6400/+0x6408/+0x6410/+0x6418, biases at +0x63fc/+0x6404/+0x640c/+0x6414, a "not at default" bit per parameter in
 * the word at +0x6628 (0x10..0x80 scales, 0x100..0x800 biases), and the current colour matrix (matrix mode 2) at +0x4e60 + 2 * 64, column-major floats.  The floats are
 * converted once per call; the per-pixel work is integer: scale Q8, bias in 8-bit units Q8, matrix Q16. */
typedef struct { int active, sq[4], bq[4], cm_on, cm[16], pm_on, psq[4], pbq[4], map_on, mn[4]; unsigned char mp[4][4096]; } pxt_state;
static int pxt_q(float f, int one, int lim) { float v = f * (float)one; if (v > (float)lim) v = (float)lim; else if (v < -(float)lim) v = -(float)lim; return (int)v; }
static inline int pxt_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static void pxt_load(const unsigned char *gc, pxt_state *t)
{
    static const unsigned so[4] = { 0x6400, 0x6408, 0x6410, 0x6418 }, bo[4] = { 0x63fc, 0x6404, 0x640c, 0x6414 }; int i;
    t->active = 0; t->cm_on = 0; t->pm_on = 0; t->map_on = 0;
    for (i = 0; i < 4; i++) { t->sq[i] = 256; t->bq[i] = 0; t->psq[i] = 256; t->pbq[i] = 0; }
    if (!gc) return;
    if (*(const uint32_t *)(gc + 0x6628) & 0xff0u) {
        t->active = 1;
        for (i = 0; i < 4; i++) { t->sq[i] = pxt_q(*(const float *)(gc + so[i]), 256, 1 << 17); t->bq[i] = pxt_q(*(const float *)(gc + bo[i]), 255 * 256, 1 << 24); }
    }
    if (*(const uint32_t *)(gc + 0x6628) & 1u) {                /* GL_MAP_COLOR: glPixelMapfv R/G/B/A_TO_x tables, pointers at +0x6470.. (float 0..1), sizes at +0x649c.. (GLEngine 0x26fd8..0x271a0) */
        static const unsigned mpo[4] = { 0x6470, 0x6474, 0x6478, 0x647c }, mno[4] = { 0x649c, 0x64a0, 0x64a4, 0x64a8 };
        for (i = 0; i < 4; i++) {
            const float *tab = (const float *)(uintptr_t)*(const uint32_t *)(gc + mpo[i]); int n = *(const int *)(gc + mno[i]), k;
            if (!tab || n < 1 || n > 4096) { t->mn[i] = 0; continue; }
            t->mn[i] = n; for (k = 0; k < n; k++) t->mp[i][k] = (unsigned char)pxt_clamp((int)(tab[k] * 255.0f + 0.5f));
        }
        t->active = 1; t->map_on = 1;
    }
    if (*(const uint32_t *)(gc + 0x6628) & 0xff0000u) {         /* GL_POST_COLOR_MATRIX_*_SCALE/BIAS (glPixelTransferf cases 0x80b4..0x80bb) */
        static const unsigned pso[4] = { 0x6570, 0x6580, 0x6578, 0x6588 }, pbo[4] = { 0x6574, 0x657c, 0x6584, 0x658c };
        t->active = 1; t->pm_on = 1;
        for (i = 0; i < 4; i++) { t->psq[i] = pxt_q(*(const float *)(gc + pso[i]), 256, 1 << 17); t->pbq[i] = pxt_q(*(const float *)(gc + pbo[i]), 255 * 256, 1 << 24); }
    }
    {   const float *m = (const float *)(gc + 0x4e60 + 2 * 64); static const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        for (i = 0; i < 16; i++) if (m[i] != id[i]) { t->cm_on = 1; break; }
        if (t->cm_on) for (i = 0; i < 16; i++) t->cm[i] = pxt_q(m[i], 65536, 1 << 20);
    }
    if (t->cm_on) t->active = 1;
}
static void pxt_apply(const pxt_state *t, unsigned char *R, unsigned char *G, unsigned char *B, int A)
{
    int c[4], k;
    c[0] = *R; c[1] = *G; c[2] = *B; c[3] = A;
    for (k = 0; k < 4; k++) if (t->sq[k] != 256 || t->bq[k]) c[k] = pxt_clamp((c[k] * t->sq[k] + t->bq[k] + 128) >> 8);
    if (t->map_on) for (k = 0; k < 4; k++) if (t->mn[k] > 1) c[k] = t->mp[k][(c[k] * (t->mn[k] - 1) * 257 + 32768) >> 16]; else if (t->mn[k] == 1) c[k] = t->mp[k][0];
    if (t->cm_on) {
        int o[3];
        for (k = 0; k < 3; k++) o[k] = pxt_clamp((c[0] * t->cm[k] + c[1] * t->cm[4 + k] + c[2] * t->cm[8 + k] + c[3] * t->cm[12 + k] + 32768) >> 16);
        c[0] = o[0]; c[1] = o[1]; c[2] = o[2];
    }
    if (t->pm_on) for (k = 0; k < 3; k++) c[k] = pxt_clamp((c[k] * t->psq[k] + t->pbq[k] + 128) >> 8);
    *R = (unsigned char)c[0]; *G = (unsigned char)c[1]; *B = (unsigned char)c[2];
}

int mach64_ctl08(void *rb_ctx, int x, int y, int w, int h, unsigned format, unsigned type, void *pixels)
{
    gld_context *c = (gld_context *)rb_ctx; mach64_priv *p = c ? MACH64_PRIV(c) : 0;
    int W, H, fr0, nr, p_align, p_rowlen, p_skippix, p_skiprows, p_swap, p_lsb, l, row_stride, row, col, is_depth = (format == 0x1902u);
    pxwin win; unsigned char *base = (unsigned char *)pixels; pxt_state pxt;
    if (!p || !pixels || w <= 0 || h <= 0 || !mach64_mode_check(c) || !p->rb.col_off) return 0;
    pxt_load(p->gc, &pxt);
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
        mach64_zs_depth_to_hw(c);                       /* the depth plane may live in the CPU copy (stencil emulation) */
        if (win_get_bpp(p, p->rb.z_off, fr0, nr, &win, 2u)) return 0;           /* the Z buffer is 16 bit whatever the colour depth */
        row_stride = (elem >= p_align) ? l * elem : p_align * ((l * elem + p_align - 1) / p_align);
        base += p_skiprows * row_stride + p_skippix * elem;
        for (row = 0; row < h; row++) {
            unsigned char *o = base + row * row_stride;
            for (col = 0; col < w; col++, o += elem) {
                unsigned char *s = win_px(&win, H, x + col, y + row, W);
                unsigned z16;
                if (!s) continue;
                z16 = px_ld(&win, s);
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
                unsigned char *s = win_px(&win, H, x + col, y + row, W);
                unsigned R = 0, G = 0, B = 0, A = 0xffu;
                if (s) { unsigned pv = px_ld(&win, s); unpack16(p, pv, &R, &G, &B); if (p->rb.bpp == 4u) A = pv >> 24; }   /* ARGB8888 stores the destination alpha (measured, alpha_test); 15/16 bpp targets report 1.0 */
                if (s && pxt.active) { unsigned char r8 = (unsigned char)R, g8 = (unsigned char)G, b8 = (unsigned char)B; pxt_apply(&pxt, &r8, &g8, &b8, 255); R = r8; G = g8; B = b8; }
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
    int fr0, nr, ylo_all, yhi_all; float zx = 1.0f, zy = 1.0f; pxwin win; pxt_state pxt;
    if (!p || !p->rb.col_off || !rp || !src || w <= 0 || h <= 0 || !fmt_decode(e, f, &is_bgr, &has_a, &pk)) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h; pxt_load(p->gc, &pxt);
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
            unsigned char R, G, B; uint32_t v;
            decode_pixel(srow + col * bpp_in, pk, is_bgr, has_a, u_swap, &R, &G, &B);
            if (pxt.active) pxt_apply(&pxt, &R, &G, &B, (!pk && has_a) ? (int)srow[col * bpp_in + 3] : 255);
            v = pack16(p, R, G, B);
            for (wy = ylo; wy < yhi; wy++) for (wx = xlo; wx < xhi; wx++) { unsigned char *o = win_px(&win, H, wx, wy, W); if (o) px_st(&win, o, v); }
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
    const float *rp = (const float *)(uintptr_t)a; pxt_state pxt; float zx = 1.0f, zy = 1.0f;
    int sx = (int)b, sy = (int)d, w = (int)e, h = (int)f, dx, dy, W, H, row, col, sfr0, snr, dfr0, dnr, ylo_all, yhi_all; pxwin src, dst;
    (void)g;
    if (!p || !p->rb.col_off || !rp || w <= 0 || h <= 0) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h; pxt_load(p->gc, &pxt);
    dx = (int)(rp[0] + 0.5f); dy = (int)(rp[1] + 0.5f);
    if (p->gc) { float rzx = *(const float *)(p->gc + 0x63bc), rzy = *(const float *)(p->gc + 0x63c0); if (rzx != 0.0f) zx = rzx; if (rzy != 0.0f) zy = rzy; }     /* glPixelZoom applies to CopyPixels too */
    { float y0 = (float)dy, y1 = (float)dy + (float)h * zy; ylo_all = iceil((y0 < y1 ? y0 : y1) - 0.5f); yhi_all = iceil((y0 < y1 ? y1 : y0) - 0.5f); }
    mach64_frame_start(c); mach64_flush_batch(c);
    if (!win_rows_for(H, sy, sy + h, &sfr0, &snr) || !win_rows_for(H, ylo_all, yhi_all, &dfr0, &dnr)) return 0;
    if (win_get(p, p->rb.col_off, sfr0, snr, &src)) return 0;
    if (win_get(p, p->rb.col_off, dfr0, dnr, &dst)) { free(src.buf); return 0; }
    for (row = 0; row < h; row++) {
        float wy0 = (float)dy + (float)row * zy, wy1 = (float)dy + (float)(row + 1) * zy;
        int ylo = iceil((wy0 < wy1 ? wy0 : wy1) - 0.5f), yhi = iceil((wy0 < wy1 ? wy1 : wy0) - 0.5f), wy;
        for (col = 0; col < w; col++) {
            unsigned char *s = win_px(&src, H, sx + col, sy + row, W); uint32_t v; float wx0 = (float)dx + (float)col * zx, wx1 = (float)dx + (float)(col + 1) * zx;
            int xlo = iceil((wx0 < wx1 ? wx0 : wx1) - 0.5f), xhi = iceil((wx0 < wx1 ? wx1 : wx0) - 0.5f), wx;
            if (!s) continue;
            v = px_ld(&src, s);
            if (pxt.active) { unsigned R, G, B; unsigned char r8, g8, b8; unpack16(p, v, &R, &G, &B); r8 = (unsigned char)R; g8 = (unsigned char)G; b8 = (unsigned char)B; pxt_apply(&pxt, &r8, &g8, &b8, 255); v = pack16(p, r8, g8, b8); }
            for (wy = ylo; wy < yhi; wy++) for (wx = xlo; wx < xhi; wx++) { unsigned char *o = win_px(&dst, H, wx, wy, W); if (o) px_st(&dst, o, v); }
        }
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
    int w = (int)b, h = (int)d, dx, dy, W, H, row, col, rowbytes, u_align, u_rowlen, u_skippix, u_skiprows, u_swap, u_lsb, lbits, fr0, nr; pxwin win; uint32_t v;
    (void)e; (void)f;
    if (!p || !p->rb.col_off || !rp || !bm || w <= 0 || h <= 0) return 0;
    W = (int)p->rb.w; H = (int)p->rb.h;
    if (p->gc) { const float *rc = (const float *)(p->gc + 0x2e4);
        v = pack16(p, (unsigned)rage_f2byte(gld_bits(rc[0])), (unsigned)rage_f2byte(gld_bits(rc[1])), (unsigned)rage_f2byte(gld_bits(rc[2]))); }
    else v = p->rb.bpp == 4u ? 0xffffffffu : 0xffffu;
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
            unsigned char *o;
            if (!(brow[bitidx >> 3] & mask)) continue;
            o = win_px(&win, H, dx + col, dy + row, W); if (o) px_st(&win, o, v);
        }
    }
    win_put(p, &win);
    return 0;
}

/* ---- accumulation buffer (table[0x00], reference glAccum_impl).  Integer fixed point: each channel is held as Q8 of a colour byte (255.0 = 255 << 8) in an int32; the value operand is
 * converted once to Q16.  The accumulation buffer covers the whole colour buffer and ignores the scissor. ---------------------------------------------------------------------- */
typedef struct { int32_t *a; int w, h; } accum_buf;
void mach64_accum_free(mach64_priv *p) { accum_buf *b = (accum_buf *)p->accum; if (b) { free(b->a); free(b); p->accum = 0; } }
static accum_buf *accum_get(mach64_priv *p)
{
    accum_buf *b = (accum_buf *)p->accum; int W = (int)p->rb.w, H = (int)p->rb.h;
    if (b && (b->w != W || b->h != H)) { mach64_accum_free(p); b = 0; }
    if (!b) {
        b = (accum_buf *)calloc(1, sizeof *b); if (!b) return 0;
        b->a = (int32_t *)calloc((size_t)W * H * 3, sizeof(int32_t)); if (!b->a) { free(b); return 0; }
        b->w = W; b->h = H; p->accum = b;
    }
    return b;
}
void mach64_accum_clear(gld_context *c)
{
    mach64_priv *p = MACH64_PRIV(c); accum_buf *b; const unsigned char *g;
    if (!p || !p->rb.col_off || !(b = accum_get(p))) return;
    {   int32_t cl[3] = { 0, 0, 0 }; int i, n = b->w * b->h; float f; (void)g;
        if (ACCUM_CLEAR_OFF) for (i = 0; i < 3; i++) { uint32_t u = mach64_gli_u32(c, ACCUM_CLEAR_OFF + (uint32_t)i * 4u); memcpy(&f, &u, 4); cl[i] = (int32_t)(f * 65280.0f); }
        for (i = 0; i < n; i++) { b->a[i * 3] = cl[0]; b->a[i * 3 + 1] = cl[1]; b->a[i * 3 + 2] = cl[2]; }
    }
}
void mach64_ctl00(gld_context *c, uint32_t op, float value)
{
    mach64_priv *p = c ? MACH64_PRIV(c) : 0; accum_buf *b; pxwin win; int W, H, x, y, fr0, nr;
    int64_t vq = (int64_t)(value * 65536.0f);
    if (!p || !p->attached || !p->rb.col_off || op < 0x100u || op > 0x104u || !(b = accum_get(p))) return;
    W = b->w; H = b->h;
    mach64_frame_start(c); mach64_flush_batch(c);
    if (op == 0x103u || op == 0x104u) {                                   /* MULT / ADD: accumulation buffer only */
        int i, n = W * H * 3;
        if (op == 0x103u) for (i = 0; i < n; i++) b->a[i] = (int32_t)(((int64_t)b->a[i] * vq) >> 16);
        else { int32_t add = (int32_t)((vq * 65280) >> 16); for (i = 0; i < n; i++) b->a[i] += add; }
        return;
    }
    if (!win_rows_for(H, 0, H, &fr0, &nr) || win_get(p, p->rb.col_off, fr0, nr, &win)) return;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        unsigned char *s = win_px(&win, H, x, y, W); int32_t *a = b->a + ((size_t)y * W + x) * 3; unsigned R, G, B;
        if (!s) continue;
        if (op == 0x102u) {                                              /* RETURN: colour = accum * value, clamped */
            int64_t r = ((int64_t)a[0] * vq) >> 24, g = ((int64_t)a[1] * vq) >> 24, bl = ((int64_t)a[2] * vq) >> 24;
            R = r < 0 ? 0 : r > 255 ? 255 : (unsigned)r; G = g < 0 ? 0 : g > 255 ? 255 : (unsigned)g; B = bl < 0 ? 0 : bl > 255 ? 255 : (unsigned)bl;
            px_st(&win, s, pack16(p, R, G, B));
        } else {                                                          /* LOAD / ACCUM: from the colour buffer */
            unpack16(p, px_ld(&win, s), &R, &G, &B);
            if (op == 0x101u) { a[0] = (int32_t)(((int64_t)R * vq) >> 8); a[1] = (int32_t)(((int64_t)G * vq) >> 8); a[2] = (int32_t)(((int64_t)B * vq) >> 8); }
            else { a[0] += (int32_t)(((int64_t)R * vq) >> 8); a[1] += (int32_t)(((int64_t)G * vq) >> 8); a[2] += (int32_t)(((int64_t)B * vq) >> 8); }
        }
    }
    if (op == 0x102u) win_put(p, &win); else { free(win.buf); win.buf = 0; }
}

/* ---- CPU compositing support (gld_stencil.c): a window of colour-buffer rows read back, modified by the CPU rasteriser, and written back.  Rows are framebuffer rows (y down). ---- */
void *mach64_cb_open(gld_context *c, int fr0, int nr)
{
    mach64_priv *p = MACH64_PRIV(c); pxwin *w;
    if (!p || !p->rb.col_off || nr <= 0) return 0;
    w = (pxwin *)malloc(sizeof *w); if (!w) return 0;
    if (win_get(p, p->rb.col_off, fr0, nr, w)) { free(w); return 0; }
    return w;
}
/* One pixel of the open window as a logical value (RGB565/1555 or ARGB8888); 0 = outside the window. */
int mach64_cb_get(void *h, int fbrow, int x, uint32_t *v)
{
    pxwin *w = (pxwin *)h; unsigned char *q;
    if (fbrow < w->fr0 || fbrow >= w->fr0 + w->nr || x < 0 || (uint32_t)x >= w->pitch) return 0;
    q = (unsigned char *)w->buf + ((uint32_t)(fbrow - w->fr0) * w->pitch + (uint32_t)x) * w->bpp;
    *v = px_ld(w, q);
    return 1;
}
void mach64_cb_put(void *h, int fbrow, int x, uint32_t v)
{
    pxwin *w = (pxwin *)h; unsigned char *q;
    if (fbrow < w->fr0 || fbrow >= w->fr0 + w->nr || x < 0 || (uint32_t)x >= w->pitch) return;
    q = (unsigned char *)w->buf + ((uint32_t)(fbrow - w->fr0) * w->pitch + (uint32_t)x) * w->bpp;
    px_st(w, q, v);
}
void mach64_cb_close(gld_context *c, void *h, int write_back)
{
    mach64_priv *p = MACH64_PRIV(c); pxwin *w = (pxwin *)h;
    if (!w) return;
    if (write_back) win_put(p, w); else free(w->buf);
    free(w);
    if (write_back) p->dirty |= MACH64_DIRTY_STATE;
}
int mach64_cb_is555(gld_context *c) { return MACH64_PRIV(c)->rb.fmt555 != 0; }
int mach64_cb_is32(gld_context *c) { return MACH64_PRIV(c)->rb.bpp == 4u; }
uint32_t mach64_cb_pack(gld_context *c, unsigned R, unsigned G, unsigned B) { return pack16(MACH64_PRIV(c), R, G, B); }
void mach64_cb_unpack(gld_context *c, unsigned v, unsigned *R, unsigned *G, unsigned *B) { unpack16(MACH64_PRIV(c), v, R, G, B); }


/* CGL off-screen drawable (attach type 0x35): copy the colour target into the app's memory.  Reference layout (generic software renderer, cgl_offscreen soft on the G4): rows top-down
 * (row 0 = top of the image), ARGB8888 words in memory byte order A,R,G,B, alpha byte 0 for a format without alpha bits.  The target holds the same rows top-down. */
int mach64_offscreen_flush(mach64_priv *p)
{
    uint32_t pb, rows, y = 0, x; unsigned char *tmp;
    if (!p || !p->offscreen || !p->off_base || !p->rb.col_off || !p->fb_w || !p->fb_h) return -1;
    pb = p->rb.pitch * p->rb.bpp;
    rows = 0x80000u / pb; if (rows < 1) rows = 1;
    tmp = (unsigned char *)malloc((size_t)rows * pb);
    if (!tmp) return -1;
    while (y < p->fb_h) {
        uint32_t n = p->fb_h - y < rows ? p->fb_h - y : rows, r;
        if (rgb_readback2(&p->rb, p->rb.col_off + y * pb, tmp, n * pb, XFER_FOR(p->rb.bpp))) { free(tmp); return -1; }
        for (r = 0; r < n; r++) {
            const unsigned char *src = tmp + (size_t)r * pb;
            unsigned char *dst = (unsigned char *)(uintptr_t)p->off_base + (size_t)(y + r) * p->off_rowbytes;
            for (x = 0; x < p->fb_w; x++) {
                unsigned R, G, B; uint32_t v = p->rb.bpp == 4u ? *(const uint32_t *)(const void *)(src + (size_t)x * 4) : *(const uint16_t *)(const void *)(src + (size_t)x * 2);
                unpack16(p, v, &R, &G, &B);
                dst[x * 4] = 0; dst[x * 4 + 1] = (unsigned char)R; dst[x * 4 + 2] = (unsigned char)G; dst[x * 4 + 3] = (unsigned char)B;
            }
        }
        y += n;
    }
    free(tmp);
    return 0;
}
