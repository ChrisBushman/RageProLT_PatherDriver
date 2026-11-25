#include "rage_backend.h"
#include <string.h>
#include <stdlib.h>
#include "rage_abi.h"
#include "mach64_regs.h"

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
    return 0;
}

int rgb_scissor(rgb_t *b, int l, int r, int t, int bt)
{ return (int)CALL(RAGE_GL_SET_SCISSOR, 4, 0, (uint32_t)l, (uint32_t)r, (uint32_t)t, (uint32_t)bt); }

/* Decode a host-order packet stream into {reg,val} pairs, routing the kernel-owned registers. */
int rgb_flush(rgb_t *b, const m64_cmdbuf *cb)
{
    uint32_t *pairs = (uint32_t *)malloc(sizeof(uint32_t) * 2u * (cb->n + 1u));
    uint32_t np = 0, tfmt = 0, tfmt2 = 0; size_t i = 0; int rc = 0; uint32_t *base = pairs, *p;
    if (!pairs) return -1;
    while (i < cb->n) {
        uint32_t h = cb->buf[i++], idx = h & 0x1ffu, cnt = ((h >> 16) & 0x3ffu) + 1u, k;
        uint32_t reg = (idx & 0x100u) ? ((idx & 0xffu) << 2) : (0x400u + (idx << 2));
        for (k = 0; k < cnt && i < cb->n; k++, reg += 4u) {
            uint32_t v = cb->buf[i++];
            if (reg == MACH64_DP_PIX_WIDTH) { tfmt = v >> 28; tfmt2 = (v >> 4) & 0xfu; continue; }                 /* kernel owns the width; texel formats go via SET_TEXTURE */
            if (reg >= MACH64_TEX_0_OFF && reg <= MACH64_TEX_0_OFF + 40u) { CALL(RAGE_GL_SET_TEXTURE, 3, 0, (reg - MACH64_TEX_0_OFF) >> 2, v, 0u); continue; }
            if (reg == MACH64_SECONDARY_TEX_OFF) { CALL(RAGE_GL_SET_TEXTURE, 3, 0, 11u, v, 0u); continue; }
            if (reg == MACH64_DST_OFF_PITCH || reg == MACH64_Z_OFF_PITCH || reg == MACH64_SC_LEFT_RIGHT || reg == MACH64_SC_TOP_BOTTOM || reg == MACH64_SRC_CNTL) { b->dropped++; continue; }
            pairs[2 * np] = reg; pairs[2 * np + 1] = v; np++;
        }
    }
    if (tfmt && tfmt != 6u) CALL(RAGE_GL_SET_TEXTURE, 3, 0, 0u, 0xfffffffeu, tfmt);
    if (tfmt2) CALL(RAGE_GL_SET_TEXTURE, 3, 0, 11u, 0xfffffffeu, tfmt2);
    p = base;
    while (np && !rc) {                                                                  /* kernel cap 8192 pairs per batch */
        uint32_t n = np > 8000u ? 8000u : np;
        rc = (int)CALL(RAGE_GL_SUBMIT, 2, 0, (uint32_t)(uintptr_t)p, n);
        if (!rc) b->fence++;
        np -= n; p += 2u * n;
    }
    free(base);
    return rc;
}

int rgb_fill(rgb_t *b, int x, int y, int w, int h, uint32_t c) { return (int)CALL(RAGE_GL_FILL, 5, 0, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, c); }
int rgb_fence(rgb_t *b) { uint32_t done; return (int)CALL(RAGE_GL_FENCE, 1, 1, b->fence, &done); }
int rgb_present(rgb_t *b, int sx, int sy, int w, int h, int dx, int dy) { return (int)CALL(RAGE_GL_PRESENT, 6, 0, (uint32_t)sx, (uint32_t)sy, (uint32_t)w, (uint32_t)h, (uint32_t)dx, (uint32_t)dy); }
int rgb_tex_alloc(rgb_t *b, uint32_t bytes, uint32_t *off) { return (int)CALL(RAGE_GL_ALLOC, 2, 1, (bytes + 4095u) & ~4095u, 4096u, off); }
int rgb_tex_free(rgb_t *b, uint32_t off) { return (int)CALL(RAGE_GL_FREE, 1, 0, off); }
int rgb_upload(rgb_t *b, uint32_t off, const void *d, uint32_t len, int swap16) { return (int)CALL(RAGE_GL_VRAM_WRITE, 4, 0, off, (uint32_t)(uintptr_t)d, len, swap16 ? RAGE_XFER_SWAP16 : 0u); }
int rgb_readback(rgb_t *b, uint32_t off, void *d, uint32_t len) { return (int)CALL(RAGE_GL_VRAM_READ, 4, 0, off, (uint32_t)(uintptr_t)d, len, 0u); }
