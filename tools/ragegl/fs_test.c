/* tools/ragegl/fs_test.c - full-screen present through the bundle at 1024x768 with a 32 bpp scanout: the colour target is 16 bpp (the 32 bpp one does not fit the VRAM), so every
 * frame goes through the kernel's textured-quad present.  Draws a triangle on a cleared target, presents, reads the scanout back, and times the presents.  Debug kext. */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "gld.h"
#include "../../tools/ragegl/ragegl.h"

extern unsigned g_gld_force_w, g_gld_force_h;
static int npass, nfail; static int g_H = 768;
#define CHECK(n, c) do { if (c) { npass++; printf("  PASS  %s\n", n); } else { nfail++; printf("  FAIL  %s\n", n); } } while (0)
static gld_context *ctx; static unsigned char *gli, table[0x80];
typedef void (*slot3_fn)(gld_context *, const void *, uint32_t);
static slot3_fn SLOT(unsigned off) { return *(slot3_fn *)(table + off); }
static void V(unsigned char *v, int x, int y, float z, unsigned argb)
{
    float xf = (float)x, yf = (float)(g_H - y), w = 1.0f, c[4]; int i;
    memset(v, 0, MACH64_GLIVERTEX_STRIDE);
    for (i = 0; i < 4; i++) c[i] = (float)((argb >> (i == 3 ? 24 : 16 - 8 * i)) & 0xff) / 255.0f;
    memcpy(v + 0x00, &xf, 4); memcpy(v + 0x04, &yf, 4); memcpy(v + 0x0c, &w, 4); memcpy(v + 0x18, &z, 4);
    memcpy(v + 0x30, &c[0], 4); memcpy(v + 0x34, &c[1], 4); memcpy(v + 0x38, &c[2], 4); memcpy(v + 0x3c, &c[3], 4);
}

static uint32_t scan_px(ragegl_t c, uint32_t pitch, uint32_t x, uint32_t y)
{ uint8_t b[4]; if (ragegl_scanout_read(c, (y * pitch + x) * 4, b, 4)) return 0xdeadbeef; return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static int isc(uint32_t v, int r, int g, int b) { int dr = (int)((v >> 16) & 255) - r, dg = (int)((v >> 8) & 255) - g, db = (int)(v & 255) - b; return dr < 9 && dr > -9 && dg < 9 && dg > -9 && db < 9 && db > -9; }
int main(void)
{
    static uint32_t infob[16];
    int attribs[] = { 8, 32, 12, 16, 5, 73, 0 }, rc, i; void *pf = 0; mach64_priv *p; unsigned char vb[3 * 0x100]; ragegl_t rg; uint32_t gen, w, h, pitch, bpp, base;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (ragegl_open(&rg)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(rg, &gen, &w, &h, &pitch, &bpp, &base);
    printf("display %ux%u bpp %u pitch %u\n", w, h, bpp, pitch);
    if (bpp != 4 || w != 1024 || h != 768) { printf("SKIP: needs a 1024x768 32 bpp scanout\n"); return 77; }
    gldInitializeLibrary(NULL, 0, 0, 0, 0);
    rc = gldChoosePixelFormat(&pf, attribs); CHECK("pixel format", !rc && pf);
    rc = gldCreateContext(&ctx, pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0); CHECK("create context", !rc && ctx);
    if (!ctx) return 1;
    g_gld_force_w = 1024; g_gld_force_h = 768;
    { unsigned char *dr = calloc(1, 0x80); rc = gldAttachDrawable(ctx, 0x36, dr, 0); }
    p = MACH64_PRIV(ctx); CHECK("attach", !rc && p->attached);
    printf("      target: %u bpp, pitch %u px\n", p->rb.bpp * 8, p->rb.pitch);
    CHECK("the target is 16 bpp with a power-of-two pitch (32 bpp does not fit)", p->rb.bpp == 2 && p->rb.pitch == 1024);
    gli = calloc(1, 0x100000);
    *(uint32_t *)(gli + 0x7898) = (uint32_t)(uintptr_t)ctx;
    *(uint32_t *)((unsigned char *)ctx + 0x10) = (uint32_t)(uintptr_t)gli + 0x3610;
    { float f = 1.0f; memcpy(gli + 0x6630, &f, 4); memcpy(gli + 0x6394, &f, 4); }
    memset(gli + 0x63b4, 1, 4);
    *(uint32_t *)(gli + 0x6708) = 1; *(uint32_t *)(gli + 0x633c) = 0x0201u << 16;
    { float cc[4] = { 0.f, 0.f, 1.f, 1.f }; memcpy(gli + 0x6320, cc, 16); }
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)infob);
    mach64_clear_color(ctx);
    { unsigned col = 0xff00ff00u;
      V(vb, 20, 20, 0.5f, col); V(vb + 0x100, 220, 20, 0.5f, col); V(vb + 0x200, 120, 220, 0.5f, col);
      SLOT(0x2c)(ctx, vb, 3); }
    mach64_present(ctx); rgb_fence(&p->rb);
    CHECK("scanout: blue background (3,3)", isc(scan_px(rg, pitch, 3, 3), 0, 0, 255));
    CHECK("scanout: blue background (1020,760)", isc(scan_px(rg, pitch, 1020, 760), 0, 0, 255));
    CHECK("scanout: green inside the triangle (120,60)", isc(scan_px(rg, pitch, 120, 60), 0, 255, 0));
    CHECK("scanout: blue outside the triangle (400,60)", isc(scan_px(rg, pitch, 400, 60), 0, 0, 255));
    { struct timeval t0, t1; double ms;
      gettimeofday(&t0, 0); for (i = 0; i < 40; i++) mach64_present(ctx); rgb_fence(&p->rb); { uint32_t d; ragegl_fence(rg, 1, &d); } (void)scan_px(rg, pitch, 3, 3); gettimeofday(&t1, 0);
      ms = ((t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0) / 40.0;
      printf("      %.2f ms per full-screen present through the bundle\n", ms); CHECK("present rate sane (< 40 ms)", ms < 40.0); }
    gldDestroyContext(ctx);
    /* phase 2: a 640x480 full-screen drawable on the 1024x768 scanout (FS_SCALE: stretched bilinear by the quad) */
    ctx = NULL; g_gld_force_w = 640; g_gld_force_h = 480; g_H = 480;
    rc = gldCreateContext(&ctx, pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0); CHECK("scale: create context", !rc && ctx);
    if (ctx) {
        { unsigned char *dr = calloc(1, 0x80); rc = gldAttachDrawable(ctx, 0x36, dr, 0); }
        p = MACH64_PRIV(ctx); CHECK("scale: attach", !rc && p->attached);
        printf("      target %ux%u, %u bits, pitch %u px, fs_scale %u\n", p->fb_w, p->fb_h, p->rb.bpp * 8, p->rb.pitch, p->fs_scale);
        CHECK("scale: fs_scale selected for a 640x480 drawable on a 1024x768 scanout", p->fs_scale == 1);
        memset(gli, 0, 0x100000);
        *(uint32_t *)(gli + 0x7898) = (uint32_t)(uintptr_t)ctx;
        *(uint32_t *)((unsigned char *)ctx + 0x10) = (uint32_t)(uintptr_t)gli + 0x3610;
        { float f = 1.0f; memcpy(gli + 0x6630, &f, 4); memcpy(gli + 0x6394, &f, 4); }
        memset(gli + 0x63b4, 1, 4);
        *(uint32_t *)(gli + 0x6708) = 1; *(uint32_t *)(gli + 0x633c) = 0x0201u << 16;
        { float cc[4] = { 0.f, 0.f, 1.f, 1.f }; memcpy(gli + 0x6320, cc, 16); }
        gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)infob);
        mach64_clear_color(ctx);
        { unsigned col = 0xff00ff00u;
          V(vb, 20, 20, 0.5f, col); V(vb + 0x100, 220, 20, 0.5f, col); V(vb + 0x200, 120, 220, 0.5f, col);
          SLOT(0x2c)(ctx, vb, 3); }
        mach64_present(ctx); rgb_fence(&p->rb);
        CHECK("scale: blue at the far corner (1020,760) - the target covers the whole scanout", isc(scan_px(rg, pitch, 1020, 760), 0, 0, 255));
        CHECK("scale: green at the stretched triangle (192,96)", isc(scan_px(rg, pitch, 192, 96), 0, 255, 0));
        CHECK("scale: blue outside the stretched triangle (640,96)", isc(scan_px(rg, pitch, 640, 96), 0, 0, 255));
        { struct timeval t0, t1; double ms;
          gettimeofday(&t0, 0); for (i = 0; i < 40; i++) mach64_present(ctx); rgb_fence(&p->rb); { uint32_t d; ragegl_fence(rg, 1, &d); } (void)scan_px(rg, pitch, 3, 3); gettimeofday(&t1, 0);
          ms = ((t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_usec - t0.tv_usec) / 1000.0) / 40.0;
          printf("      %.2f ms per stretched present\n", ms); CHECK("scaled present rate sane (< 40 ms)", ms < 40.0); }
        gldDestroyContext(ctx);
    }
    gldDestroyPixelFormat(pf);
    printf("FS_TEST: %d passed, %d failed\n", npass, nfail);
    return nfail ? 2 : 0;
}
