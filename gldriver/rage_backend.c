#include <stdio.h>
#include "rage_backend.h"
#include <string.h>
#include <stdlib.h>
#include "rage_abi.h"
#include "mach64_regs.h"

#include <mach/mach_time.h>
/* RAGE_STATS=1: once a second on stderr: flushes, register pairs, time inside the submit calls, other kernel calls (fill/clear-Z/present) */
static uint64_t g_st_kn[3], g_st_kc[3];     /* per kind (0 fill, 1 clear-Z, 2 present): ns and calls */
static int g_stats = -1; static uint64_t g_st_t0, g_st_flush_ns, g_st_other_ns, g_st_nfl, g_st_pairs, g_st_nother, g_st_frames, g_st_fence_ns;
static uint64_t st_now(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * tb.numer / tb.denom; }
static void st_report(uint64_t now)
{
    if (!g_st_t0) { g_st_t0 = now; return; }
    if (now - g_st_t0 < 1000000000ull) return;
    fprintf(stderr, "RAGE_STATS: %llu frames, %llu flushes (%llu pairs, avg %llu), %llu ms in submit; %llu other calls, %llu ms, %llu ms waiting for the chip after submit (RAGE_STATS=2); fill %llu/%llu ms, clearZ %llu/%llu ms, present %llu/%llu ms\n", (unsigned long long)g_st_frames, (unsigned long long)g_st_nfl, (unsigned long long)g_st_pairs,
            (unsigned long long)(g_st_nfl ? g_st_pairs / g_st_nfl : 0), (unsigned long long)(g_st_flush_ns / 1000000ull), (unsigned long long)g_st_nother, (unsigned long long)(g_st_other_ns / 1000000ull), (unsigned long long)(g_st_fence_ns / 1000000ull), (unsigned long long)g_st_kc[0], (unsigned long long)(g_st_kn[0]/1000000ull), (unsigned long long)g_st_kc[1], (unsigned long long)(g_st_kn[1]/1000000ull), (unsigned long long)g_st_kc[2], (unsigned long long)(g_st_kn[2]/1000000ull));
    g_st_t0 = now; g_st_flush_ns = g_st_other_ns = g_st_fence_ns = g_st_nfl = g_st_pairs = g_st_nother = g_st_frames = 0; g_st_kn[0] = g_st_kn[1] = g_st_kn[2] = g_st_kc[0] = g_st_kc[1] = g_st_kc[2] = 0;
}
#define STAT_ON() (g_stats < 0 ? (g_stats = getenv("RAGE_STATS") ? (getenv("RAGE_STATS")[0] == '2' ? 2 : 1) : 0) : g_stats)
#define CALL(sel, nin, nout, ...) IOConnectMethodScalarIScalarO(b->conn, sel, nin, nout, __VA_ARGS__)

int rgb_open(rgb_t *b)
{
    mach_port_t m; io_service_t svc; kern_return_t kr; uint32_t end;
    memset(b, 0, sizeof *b);
    IOMasterPort(MACH_PORT_NULL, &m);
    svc = IOServiceGetMatchingService(m, IOServiceMatching("RageLTDevice"));
    if (!svc) return -1;
    kr = IOServiceOpen(svc, mach_task_self(), RAGE_CLIENT_GL, &b->conn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) return (int)kr;
    kr = CALL(RAGE_GL_INFO, 0, 4, &b->gen, &b->features, &end, &b->flags);
    if (kr || !(b->flags & 1u)) { IOServiceClose(b->conn); return kr ? (int)kr : -2; }
    return 0;
}

void rgb_close(rgb_t *b) { if (b->conn) IOServiceClose(b->conn); b->conn = 0; }

int rgb_surface(rgb_t *b, uint32_t w, uint32_t h, uint32_t bpp, int with_z)
{
    uint32_t pitch = (w + 7u) & ~7u, bytes = pitch * h * bpp, zbytes = pitch * h * 2u, off, zoff = 0; kern_return_t kr;
    if (b->pot_pitch && pitch <= 1024u) { uint32_t pp = 8u; while (pp < pitch) pp <<= 1; pitch = pp; }   /* square power-of-two texture alias of the target (kernel textured-quad present) */
    bytes = pitch * h * bpp; zbytes = pitch * h * 2u;
    (void)rgb_gen(b);                                  /* refresh b->flags: the target format follows the scanout depth */
    rgb_overlay_off(b);                                 /* the scaler must stop reading before its blocks are freed */
    if (b->col_off) { CALL(RAGE_GL_FREE, 1, 0, b->col_off); b->col_off = 0; }
    if (b->col_alt) { CALL(RAGE_GL_FREE, 1, 0, b->col_alt); b->col_alt = 0; }
    if (b->z_off) { CALL(RAGE_GL_FREE, 1, 0, b->z_off); b->z_off = 0; }
    kr = CALL(RAGE_GL_ALLOC, 2, 1, (bytes + 4095u) & ~4095u, 4096u, &off);
    if (kr) return (int)kr;
    if (with_z) {
        kr = CALL(RAGE_GL_ALLOC, 2, 1, (zbytes + 4095u) & ~4095u, 4096u, &zoff);
        if (kr) { CALL(RAGE_GL_FREE, 1, 0, off); return (int)kr; }
    }
    kr = CALL(RAGE_GL_SET_TARGET, 5, 0, off, pitch, bpp, w, h);
    if (!kr && with_z) kr = CALL(RAGE_GL_SET_Z, 2, 0, zoff, pitch);
    if (kr) { CALL(RAGE_GL_FREE, 1, 0, off); if (zoff) CALL(RAGE_GL_FREE, 1, 0, zoff); return (int)kr; }
    b->col_off = off; b->z_off = zoff; b->w = w; b->h = h; b->pitch = pitch; b->bpp = bpp;
    b->col_alt = 0;
    if (b->dbl) { uint32_t alt = 0; if (!CALL(RAGE_GL_ALLOC, 2, 1, (bytes + 4095u) & ~4095u, 4096u, &alt)) b->col_alt = alt; }   /* no room for a second block: single buffered, the quad present is used */
    b->fmt555 = (bpp == 2 && (b->flags & RAGE_INFO_TGT555)) ? 1 : 0;
    return 0;
}

uint32_t rgb_gen(rgb_t *b)
{ uint32_t g = 0, f = 0, e = 0, fl = 0; if (CALL(RAGE_GL_INFO, 0, 4, &g, &f, &e, &fl)) return 1u; b->features = f; b->flags = fl; return g; }
int rgb_display(rgb_t *b, uint32_t *w, uint32_t *h, uint32_t *bpp)
{ uint32_t g, ww, hh, pitch, bb, base; kern_return_t kr = CALL(RAGE_GL_DISPLAY, 0, 6, &g, &ww, &hh, &pitch, &bb, &base); if (kr) return (int)kr; if (w) *w = ww; if (h) *h = hh; if (bpp) *bpp = bb; return 0; }
void rgb_release(rgb_t *b)
{
    rgb_overlay_off(b);
    if (b->col_alt) { CALL(RAGE_GL_FREE, 1, 0, b->col_alt); b->col_alt = 0; }
    if (b->col_off) { CALL(RAGE_GL_FREE, 1, 0, b->col_off); b->col_off = 0; }
    if (b->z_off) { CALL(RAGE_GL_FREE, 1, 0, b->z_off); b->z_off = 0; }
}
int rgb_scissor(rgb_t *b, int l, int r, int t, int bt)
{ return (int)CALL(RAGE_GL_SET_SCISSOR, 4, 0, (uint32_t)l, (uint32_t)r, (uint32_t)t, (uint32_t)bt); }

/* Register routing, one byte per register (reg>>2): 0 = pass to the kernel as a {reg,val} pair, 1 = DP_PIX_WIDTH (texel formats), 2 = primary texture register (sent with the
 * submit), 3 = secondary texture offset, 4 = kernel-owned (dropped).  Replaces a chain of up to ten compares per word with one load; built once. */
#define RT_N 0x300u
static unsigned char rt_class[RT_N]; static int rt_ready;
static void rt_build(void)
{
    uint32_t r;
    for (r = 0; r < RT_N * 4u; r += 4u) {
        unsigned char c = 0;
        if (r == MACH64_DP_PIX_WIDTH) c = 1;
        else if (r >= MACH64_TEX_0_OFF && r <= MACH64_TEX_0_OFF + 40u) c = 2;
        else if (r == MACH64_SECONDARY_TEX_OFF) c = 3;
        else if (r == MACH64_DST_OFF_PITCH || r == MACH64_Z_OFF_PITCH || r == MACH64_SC_LEFT_RIGHT || r == MACH64_SC_TOP_BOTTOM || r == MACH64_SRC_CNTL) c = 4;
        rt_class[r >> 2] = c;
    }
    rt_ready = 1;
}

/* Decode a host-order packet stream into {reg,val} pairs, routing the kernel-owned registers. */
int rgb_flush(rgb_t *b, const m64_cmdbuf *cb)
{
    static uint32_t *stage; static size_t stage_cap;                                    /* one growing staging buffer: no malloc/free per flush */
    uint32_t *pairs, np = 0, tfmt = 0, tfmt2 = 0, tex[24], ntex = 0; size_t i = 0; int rc = 0; uint32_t *base, *p; int first = 1;
    if (stage_cap < 2u * (cb->n + 1u)) { free(stage); stage_cap = 2u * (cb->n + 1u) + 4096u; stage = (uint32_t *)malloc(sizeof(uint32_t) * stage_cap); if (!stage) { stage_cap = 0; return -1; } }
    pairs = base = stage; if (!rt_ready) rt_build();
    while (i < cb->n) {
        uint32_t h = cb->buf[i++], idx = h & 0x1ffu, cnt = ((h >> 16) & 0x3ffu) + 1u, k;
        uint32_t reg = (idx & 0x100u) ? ((idx & 0xffu) << 2) : (0x400u + (idx << 2));
        for (k = 0; k < cnt && i < cb->n; k++, reg += 4u) {
            uint32_t v = cb->buf[i++];
            unsigned cls = (reg >> 2) < RT_N ? rt_class[reg >> 2] : 0u;
            if (cls) {
                if (cls == 1u) { tfmt = v >> 28; tfmt2 = (v >> 4) & 0xfu; }                                         /* kernel owns the width; texel formats go via SET_TEXTURE */
                else if (cls == 2u) { if (ntex < 12u) { tex[2 * ntex] = (reg - MACH64_TEX_0_OFF) >> 2; tex[2 * ntex + 1] = v; ntex++; } }   /* sent with the submit */
                else if (cls == 3u) { if (ntex < 12u) { tex[2 * ntex] = 11u; tex[2 * ntex + 1] = v; ntex++; } }
                else b->dropped++;
                continue;
            }
            pairs[2 * np] = reg; pairs[2 * np + 1] = v; np++;
        }
    }
    uint64_t t_st = STAT_ON() ? st_now() : 0;
    p = base;
    if (!np && (ntex || (tfmt && tfmt != 6u) || tfmt2)) CALL(RAGE_GL_SUBMIT_EX, 6, 0, 0u, 0u, (uint32_t)(uintptr_t)tex, ntex, tfmt, tfmt2);
    while (np && !rc) {                                                                  /* kernel cap 8192 pairs per batch */
        uint32_t n = np > 8000u ? 8000u : np;
        if (first) rc = (int)CALL(RAGE_GL_SUBMIT_EX, 6, 0, (uint32_t)(uintptr_t)p, n, (uint32_t)(uintptr_t)tex, ntex, tfmt, tfmt2);   /* textures + submit: one kernel call */
        else rc = (int)CALL(RAGE_GL_SUBMIT, 2, 0, (uint32_t)(uintptr_t)p, n);
        first = 0;
        if (!rc) b->fence++; else b->last_err = rc;
        np -= n; p += 2u * n;
    }
    if (t_st) { uint64_t n2 = st_now(), n3; g_st_flush_ns += n2 - t_st;
        if (g_stats == 2) { uint32_t done; CALL(RAGE_GL_FENCE, 1, 1, b->fence, &done); n3 = st_now(); g_st_fence_ns += n3 - n2; n2 = n3; }       /* serialise: how long the chip still needed */ g_st_nfl++; g_st_pairs += (uint64_t)(p - base) / 2u; st_report(n2); }
    return rc;
}


static int st_other(int rc, uint64_t t0, int kind) { if (t0) { uint64_t n = st_now(); g_st_other_ns += n - t0; g_st_nother++; g_st_kn[kind] += n - t0; g_st_kc[kind]++; st_report(n); } return rc; }
int rgb_fill(rgb_t *b, int x, int y, int w, int h, uint32_t c) { uint64_t t0 = STAT_ON() ? st_now() : 0; return st_other((int)CALL(RAGE_GL_FILL, 5, 0, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, c), t0, 0); }
int rgb_clear_z(rgb_t *b, uint32_t v) { uint64_t t0 = STAT_ON() ? st_now() : 0; return st_other((int)CALL(RAGE_GL_CLEAR_Z, 1, 0, v), t0, 1); }
int rgb_fence(rgb_t *b) { uint32_t done; return (int)CALL(RAGE_GL_FENCE, 1, 1, b->fence, &done); }
int rgb_present_scaled(rgb_t *b, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int bilinear)
{ return (int)CALL(RAGE_GL_PRESENT_SCALED, 5, 0, (uint32_t)sx | ((uint32_t)sy << 16), (uint32_t)sw | ((uint32_t)sh << 16), (uint32_t)dx | ((uint32_t)dy << 16), (uint32_t)dw | ((uint32_t)dh << 16), (uint32_t)bilinear); }
int rgb_present(rgb_t *b, int sx, int sy, int w, int h, int dx, int dy) { uint64_t t0 = STAT_ON() ? st_now() : 0; g_st_frames++; return st_other((int)CALL(RAGE_GL_PRESENT, 6, 0, (uint32_t)sx, (uint32_t)sy, (uint32_t)w, (uint32_t)h, (uint32_t)dx, (uint32_t)dy), t0, 2); }
int rgb_tex_alloc(rgb_t *b, uint32_t bytes, uint32_t *off) { return (int)CALL(RAGE_GL_ALLOC, 2, 1, (bytes + 4095u) & ~4095u, 4096u, off); }
int rgb_tex_free(rgb_t *b, uint32_t off) { return (int)CALL(RAGE_GL_FREE, 1, 0, off); }
int rgb_upload(rgb_t *b, uint32_t off, const void *d, uint32_t len, int swap16) { return (int)CALL(RAGE_GL_VRAM_WRITE, 4, 0, off, (uint32_t)(uintptr_t)d, len, swap16 ? RAGE_XFER_SWAP16 : 0u); }
int rgb_upload2(rgb_t *b, uint32_t off, const void *d, uint32_t len, uint32_t fl) { return (int)CALL(RAGE_GL_VRAM_WRITE, 4, 0, off, (uint32_t)(uintptr_t)d, len, fl); }
int rgb_upload_rows(rgb_t *b, uint32_t off, const void *d, uint32_t rowbytes, uint32_t nrows, uint32_t sp, uint32_t dp, uint32_t fl) { return (int)CALL(RAGE_GL_VRAM_WRITE_ROWS, 6, 0, off, (uint32_t)(uintptr_t)d, rowbytes | (nrows << 16), sp, dp, fl); }
int rgb_readback2(rgb_t *b, uint32_t off, void *d, uint32_t len, uint32_t fl) { return (int)CALL(RAGE_GL_VRAM_READ, 4, 0, off, (uint32_t)(uintptr_t)d, len, fl); }
int rgb_readback(rgb_t *b, uint32_t off, void *d, uint32_t len) { return (int)CALL(RAGE_GL_VRAM_READ, 4, 0, off, (uint32_t)(uintptr_t)d, len, 0u); }

/* ---- hardware overlay scaler present (design: docs/re-notes/overlay-scaler-plan.md) ----
 * Double buffered: the engine draws into one block while the scaler shows the other; presenting = wait for the frame, point the scaler at it (OVL_ON the first time,
 * OVL_FLIP afterwards), then make the other block the render target.  The first/next frame is drawn into the block the scaler is NOT reading.  The scaler filters RGB
 * sources only by replication (measured), so this path is nearest-neighbour, unlike the bilinear quad; RAGE_GL_OVERLAY=0 selects the quad. */
int rgb_overlay_wanted(const rgb_t *b, uint32_t src_w)
{
    static int env = -1;
    if (env < 0) { const char *e = getenv("RAGE_GL_OVERLAY"); env = (e && e[0] == '0') ? 0 : 1; }
    return env && (b->features & RAGE_FBIT_OVERLAY) && src_w <= 720u;
}
void rgb_overlay_off(rgb_t *b)
{
    if (b->ovl_on) { (void)IOConnectMethodScalarIScalarO(b->conn, RAGE_GL_OVL_OFF, 0, 0); b->ovl_on = 0; }
}
int rgb_overlay_present(rgb_t *b, uint32_t dw, uint32_t dh)
{
    kern_return_t kr; uint32_t cur = b->col_off, alt = b->col_alt;
    if (!alt || b->ovl_fail) return -1;
    (void)rgb_fence(b);                                                    /* the frame must be complete before the scaler shows it */
    if (b->ovl_on) {
        kr = CALL(RAGE_GL_OVL_FLIP, 1, 0, cur);
        if (kr) b->ovl_on = 0;                                             /* disabled behind our back (mode change): enable it again below */
    }
    if (!b->ovl_on) {
        kr = CALL(RAGE_GL_OVL_ON, 6, 0, cur, b->pitch, b->w | (b->h << 16), 0u, dw | (dh << 16), b->bpp);
        if (kr) { b->ovl_fail = 1; return (int)kr; }                       /* refused (another client owns it, unsupported, ...): the quad takes over */
        b->ovl_on = 1;
    } else kr = 0;
    kr = CALL(RAGE_GL_SWAP_TARGET, 1, 0, alt);                             /* the other block receives the frame and becomes the target: it still holds the frame after a present (like the quad present); scissor and Z stay bound */
    if (kr) { rgb_overlay_off(b); b->ovl_fail = 1; return (int)kr; }       /* nothing was swapped: the old target is still bound */
    b->col_off = alt; b->col_alt = cur;
    return 0;
}
