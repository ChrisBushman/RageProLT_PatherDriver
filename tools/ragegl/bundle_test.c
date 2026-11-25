/* bundle_test.c - drive the new GL bundle through GLEngine's dispatch table with a stand-in gli (headless), read pixels back via the kernel. */
#include <stdio.h>
#include <string.h>
#include "gld.h"

extern unsigned g_gld_force_w, g_gld_force_h;
#define W 256
#define H 256
static int npass, nfail;
#define CHECK(n, c) do { if (c) { npass++; printf("  PASS  %s\n", n); } else { nfail++; printf("  FAIL  %s\n", n); } } while (0)

static gld_context *ctx; static unsigned char *gli, table[0x80];
typedef void (*slot3_fn)(gld_context *, const void *, uint32_t);
static slot3_fn SLOT(unsigned off) { return *(slot3_fn *)(table + off); }

static void V(unsigned char *v, int x, int y, float z, unsigned argb)
{
    float xf = (float)x, yf = (float)(H - y), w = 1.0f, c[4]; int i;
    memset(v, 0, MACH64_GLIVERTEX_STRIDE);
    for (i = 0; i < 4; i++) c[i] = (float)((argb >> (i == 3 ? 24 : 16 - 8 * i)) & 0xff) / 255.0f;
    memcpy(v + 0x00, &xf, 4); memcpy(v + 0x04, &yf, 4); memcpy(v + 0x0c, &w, 4); memcpy(v + 0x18, &z, 4);
    memcpy(v + 0x30, &c[0], 4); memcpy(v + 0x34, &c[1], 4); memcpy(v + 0x38, &c[2], 4); memcpy(v + 0x3c, &c[3], 4);
}
static uint32_t rd16(mach64_priv *p, int x, int y)
{
    uint32_t w2[2]; uint32_t o = p->rb.col_off + (uint32_t)y * p->rb.pitch * 2 + (uint32_t)x * 2;
    rgb_readback(&p->rb, o & ~3u, w2, 4);
    { uint32_t w = w2[0]; uint32_t h = (o & 2) ? (w >> 16) : (w & 0xffff); return ((h & 0xffu) << 8) | (h >> 8); }   /* VRAM is little-endian: swap the halfword */
}
int main(void)
{
    int attribs[] = { 8, 32, 12, 16, 5, 73, 0 }, rc; void *pf = 0; mach64_priv *p; static uint32_t info[16];
    unsigned char vb[3 * 0x100]; uint32_t c;
    setvbuf(stdout, NULL, _IONBF, 0);
    gldInitializeLibrary(NULL, 0, 0, 0, 0);
    rc = gldChoosePixelFormat(&pf, attribs); CHECK("pixel format", !rc && pf);
    rc = gldCreateContext(&ctx, pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0); CHECK("create context", !rc && ctx);
    if (!ctx) return 1;
    g_gld_force_w = W; g_gld_force_h = H;
    { unsigned char *dr = calloc(1, 0x80); rc = gldAttachDrawable(ctx, 0x36, dr, 0); }
    p = MACH64_PRIV(ctx); CHECK("attach", !rc && p->attached);
    gli = calloc(1, 0x100000);
    *(uint32_t *)(gli + 0x7898) = (uint32_t)(uintptr_t)ctx;
    *(uint32_t *)((unsigned char *)ctx + 0x10) = (uint32_t)(uintptr_t)gli + 0x3610;
    { float f = 1.0f; memcpy(gli + 0x6630, &f, 4); memcpy(gli + 0x6394, &f, 4); }
    memset(gli + 0x63b4, 1, 4);   /* glColorMask all on */
    *(uint32_t *)(gli + 0x6708) = 1; *(uint32_t *)(gli + 0x633c) = 0x0201u << 16;
    { float cc[4] = { 0.f, 0.f, 1.f, 1.f }; memcpy(gli + 0x6320, cc, 16); }   /* clear colour blue */
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)info);
    { unsigned als[5] = { 0x00, 0x40, 0x80, 0xc0, 0xff }; int k;
      *(uint32_t *)(gli + 0x6304) = 0x01000000u; *(uint32_t *)(gli + 0x62e8) = (0x0302u << 16) | 0x0303u;   /* blend SRC_ALPHA, 1-SRC_ALPHA */
      for (k = 0; k < 5; k++) {
        typedef void (*rc_fn)(gld_context *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        unsigned col = (als[k] << 24) | 0x0000ff00u;
        V(vb, 20, 20, 0.5f, col); V(vb + 0x100, 220, 20, 0.5f, col); V(vb + 0x200, 120, 220, 0.5f, col);
        SLOT(0x2c)(ctx, vb, 3);
        (*(rc_fn *)(table + 0x60))(ctx, 0, 0, 0, 0, 0);
        rgb_fence(&p->rb);
        printf("alpha %02x -> centre %04x corner %04x\n", als[k], rd16(p, 120, 60), rd16(p, 3, 3));
      } }
    printf("triangles %u dropped %u\n", p->triangles, p->rb.dropped);
    gldDestroyContext(ctx); gldDestroyPixelFormat(pf);
    printf("BUNDLE: %d passed, %d failed\n", npass, nfail);
    return nfail ? 2 : 0;
}
