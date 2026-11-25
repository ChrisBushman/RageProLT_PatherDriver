#include <stdio.h>
#include "rage_backend.h"
#include <string.h>
#include <stdlib.h>
#include "rage_abi.h"
#include "mach64_regs.h"

#include <mach/mach_time.h>
/* RAGE_STATS=1: once a second on stderr: flushes, register pairs, time inside the submit calls, other kernel calls (fill/clear-Z/present) */
static int g_stats = -1; static uint64_t g_st_t0, g_st_flush_ns, g_st_other_ns, g_st_nfl, g_st_pairs, g_st_nother, g_st_frames, g_st_fence_ns;
static uint64_t st_now(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * tb.numer / tb.denom; }
static void st_report(uint64_t now)
{
    if (!g_st_t0) { g_st_t0 = now; return; }
    if (now - g_st_t0 < 1000000000ull) return;
    fprintf(stderr, "RAGE_STATS: %llu frames, %llu flushes (%llu pairs, avg %llu), %llu ms in submit; %llu other calls, %llu ms, %llu ms waiting for the chip after submit (RAGE_STATS=2)\n", (unsigned long long)g_st_frames, (unsigned long long)g_st_nfl, (unsigned long long)g_st_pairs,
            (unsigned long long)(g_st_nfl ? g_st_pairs / g_st_nfl : 0), (unsigned long long)(g_st_flush_ns / 1000000ull), (unsigned long long)g_st_nother, (unsigned long long)(g_st_other_ns / 1000000ull), (unsigned long long)(g_st_fence_ns / 1000000ull));
    g_st_t0 = now; g_st_flush_ns = g_st_other_ns = g_st_fence_ns = g_st_nfl = g_st_pairs = g_st_nother = g_st_frames = 0;
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
    (void)rgb_gen(b);                                  /* refresh b->flags: the target format follows the scanout depth */
    if (b->col_off) { CALL(RAGE_GL_FREE, 1, 0, b->col_off); b->col_off = 0; }
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
    b->fmt555 = (bpp == 2 && (b->flags & RAGE_INFO_TGT555)) ? 1 : 0;
    return 0;
}

uint32_t rgb_gen(rgb_t *b)
{ uint32_t g = 0, f = 0, e = 0, fl = 0; if (CALL(RAGE_GL_INFO, 0, 4, &g, &f, &e, &fl)) return 1u; b->features = f; b->flags = fl; return g; }
int rgb_display(rgb_t *b, uint32_t *w, uint32_t *h, uint32_t *bpp)
{ uint32_t g, ww, hh, pitch, bb, base; kern_return_t kr = CALL(RAGE_GL_DISPLAY, 0, 6, &g, &ww, &hh, &pitch, &bb, &base); if (kr) return (int)kr; if (w) *w = ww; if (h) *h = hh; if (bpp) *bpp = bb; return 0; }
void rgb_release(rgb_t *b)
{
    if (b->col_off) { CALL(RAGE_GL_FREE, 1, 0, b->col_off); b->col_off = 0; }
    if (b->z_off) { CALL(RAGE_GL_FREE, 1, 0, b->z_off); b->z_off = 0; }
}
int rgb_scissor(rgb_t *b, int l, int r, int t, int bt)
{ return (int)CALL(RAGE_GL_SET_SCISSOR, 4, 0, (uint32_t)l, (uint32_t)r, (uint32_t)t, (uint32_t)bt); }

/* Decode a host-order packet stream into {reg,val} pairs, routing the kernel-owned registers. */
int rgb_flush(rgb_t *b, const m64_cmdbuf *cb)
{
    static uint32_t *stage; static size_t stage_cap;                                    /* one growing staging buffer: no malloc/free per flush */
    uint32_t *pairs, np = 0, tfmt = 0, tfmt2 = 0, tex[24], ntex = 0; size_t i = 0; int rc = 0; uint32_t *base, *p; int first = 1;
    if (stage_cap < 2u * (cb->n + 1u)) { free(stage); stage_cap = 2u * (cb->n + 1u) + 4096u; stage = (uint32_t *)malloc(sizeof(uint32_t) * stage_cap); if (!stage) { stage_cap = 0; return -1; } }
    pairs = base = stage;
    while (i < cb->n) {
        uint32_t h = cb->buf[i++], idx = h & 0x1ffu, cnt = ((h >> 16) & 0x3ffu) + 1u, k;
        uint32_t reg = (idx & 0x100u) ? ((idx & 0xffu) << 2) : (0x400u + (idx << 2));
        for (k = 0; k < cnt && i < cb->n; k++, reg += 4u) {
            uint32_t v = cb->buf[i++];
            if (reg == MACH64_DP_PIX_WIDTH) { tfmt = v >> 28; tfmt2 = (v >> 4) & 0xfu; continue; }                 /* kernel owns the width; texel formats go via SET_TEXTURE */
            if (reg >= MACH64_TEX_0_OFF && reg <= MACH64_TEX_0_OFF + 40u) { if (ntex < 12u) { tex[2 * ntex] = (reg - MACH64_TEX_0_OFF) >> 2; tex[2 * ntex + 1] = v; ntex++; } continue; }   /* sent with the submit */
            if (reg == MACH64_SECONDARY_TEX_OFF) { if (ntex < 12u) { tex[2 * ntex] = 11u; tex[2 * ntex + 1] = v; ntex++; } continue; }
            if (reg == MACH64_DST_OFF_PITCH || reg == MACH64_Z_OFF_PITCH || reg == MACH64_SC_LEFT_RIGHT || reg == MACH64_SC_TOP_BOTTOM || reg == MACH64_SRC_CNTL) { b->dropped++; continue; }
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


static int st_other(int rc, uint64_t t0) { if (t0) { uint64_t n = st_now(); g_st_other_ns += n - t0; g_st_nother++; st_report(n); } return rc; }
int rgb_fill(rgb_t *b, int x, int y, int w, int h, uint32_t c) { uint64_t t0 = STAT_ON() ? st_now() : 0; return st_other((int)CALL(RAGE_GL_FILL, 5, 0, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, c), t0); }
int rgb_clear_z(rgb_t *b, uint32_t v) { uint64_t t0 = STAT_ON() ? st_now() : 0; return st_other((int)CALL(RAGE_GL_CLEAR_Z, 1, 0, v), t0); }
int rgb_fence(rgb_t *b) { uint32_t done; return (int)CALL(RAGE_GL_FENCE, 1, 1, b->fence, &done); }
int rgb_present(rgb_t *b, int sx, int sy, int w, int h, int dx, int dy) { uint64_t t0 = STAT_ON() ? st_now() : 0; g_st_frames++; return st_other((int)CALL(RAGE_GL_PRESENT, 6, 0, (uint32_t)sx, (uint32_t)sy, (uint32_t)w, (uint32_t)h, (uint32_t)dx, (uint32_t)dy), t0); }
int rgb_tex_alloc(rgb_t *b, uint32_t bytes, uint32_t *off) { return (int)CALL(RAGE_GL_ALLOC, 2, 1, (bytes + 4095u) & ~4095u, 4096u, off); }
int rgb_tex_free(rgb_t *b, uint32_t off) { return (int)CALL(RAGE_GL_FREE, 1, 0, off); }
int rgb_upload(rgb_t *b, uint32_t off, const void *d, uint32_t len, int swap16) { return (int)CALL(RAGE_GL_VRAM_WRITE, 4, 0, off, (uint32_t)(uintptr_t)d, len, swap16 ? RAGE_XFER_SWAP16 : 0u); }
int rgb_upload2(rgb_t *b, uint32_t off, const void *d, uint32_t len, uint32_t fl) { return (int)CALL(RAGE_GL_VRAM_WRITE, 4, 0, off, (uint32_t)(uintptr_t)d, len, fl); }
int rgb_readback2(rgb_t *b, uint32_t off, void *d, uint32_t len, uint32_t fl) { return (int)CALL(RAGE_GL_VRAM_READ, 4, 0, off, (uint32_t)(uintptr_t)d, len, fl); }
int rgb_readback(rgb_t *b, uint32_t off, void *d, uint32_t len) { return (int)CALL(RAGE_GL_VRAM_READ, 4, 0, off, (uint32_t)(uintptr_t)d, len, 0u); }
