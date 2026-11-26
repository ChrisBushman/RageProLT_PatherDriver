/* tools/ragegl/fs_test.c - full-screen present through the bundle at 1024x768 with a 32 bpp scanout: the colour target is 16 bpp (the 32 bpp one does not fit the VRAM), so every
* frame goes through the kernel's textured-quad present.  Draws a triangle on a cleared target, presents, reads the scanout back, and times the presents.  Debug kext. */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
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


/* ovl_gate: gate for the hardware-overlay present of a smaller full-screen drawable (release kext compatible: no debug selectors).
 * usage: ovl_gate            run the checks and time quad vs overlay frames (RAGE_GL_OVERLAY=0 in the environment forces the quad)
 *        ovl_gate hold       set the overlay up, print READY, wait forever (kill -9 it to test the kernel's cleanup)
 *        ovl_gate status     exit 0 when no client has the overlay enabled, 3 when one has */
static uint32_t rd16(rgb_t *c, uint32_t off, uint32_t x, uint32_t y, uint32_t pitch_px)
{ uint8_t b[16]; uint32_t a = (off + (y * pitch_px + x) * 2u) & ~15u, i = ((y * pitch_px + x) * 2u) & 15u; if (rgb_readback(c, a, b, 16)) return 0xdead; return (uint32_t)b[i] | ((uint32_t)b[i + 1] << 8); }
static int is_blue(const mach64_priv *p, uint32_t v) { return (v & 0x1f) == 0x1f && ((v >> 5) & 0x1f) == 0 && ((v >> (p->rb.fmt555 ? 10 : 11)) & 0x1f) == 0; }
static int is_green(const mach64_priv *p, uint32_t v) { uint32_t g = p->rb.fmt555 ? ((v >> 5) & 0x1f) : ((v >> 5) & 0x3f); return g == (p->rb.fmt555 ? 0x1fu : 0x3fu) && (v & 0x1f) == 0 && ((v >> (p->rb.fmt555 ? 10 : 11)) & 0x1f) == 0; }
static int is_red(const mach64_priv *p, uint32_t v) { return ((v >> (p->rb.fmt555 ? 10 : 11)) & 0x1f) == 0x1f && (v & 0x1f) == 0 && ((v >> 5) & (p->rb.fmt555 ? 0x1fu : 0x3fu)) == 0; }
static void tri(uint32_t col, int ox)
{
    unsigned char vb[3 * 0x100];
    V(vb, ox + 20, 20, 0.5f, col); V(vb + 0x100, ox + 220, 20, 0.5f, col); V(vb + 0x200, ox + 120, 220, 0.5f, col);
    SLOT(0x2c)(ctx, vb, 3);
}
static double now_ms(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec * 1000.0 + t.tv_usec / 1000.0; }
int main(int argc, char **argv)
{
    static uint32_t infob[16];
    int attribs[] = { 8, 32, 12, 16, 5, 73, 0 }, rc, i; void *pf = 0; mach64_priv *p; ragegl_t rg; uint32_t gen, feat, endv, flags, w, h, pitch, bpp, base; int hold = argc > 1 && !strcmp(argv[1], "hold");
    const char *envv = getenv("RAGE_GL_OVERLAY"); int want_ovl = !(envv && envv[0] == '0');
    setvbuf(stdout, NULL, _IONBF, 0);
    if (ragegl_open(&rg)) { printf("cannot open the GL client\n"); return 99; }
    if (argc > 1 && !strcmp(argv[1], "status")) { ragegl_info(rg, &gen, &feat, &endv, &flags); printf("overlay %s\n", (flags & RAGE_INFO_OVL) ? "ENABLED" : "idle"); return (flags & RAGE_INFO_OVL) ? 3 : 0; }
    ragegl_display(rg, &gen, &w, &h, &pitch, &bpp, &base);
    printf("display %ux%u bpp %u, overlay %s\n", w, h, bpp, want_ovl ? "wanted (default)" : "disabled by RAGE_GL_OVERLAY=0");
    if (w < 641 || h < 481) { printf("SKIP: needs a scanout larger than 640x480\n"); return 77; }
    gldInitializeLibrary(NULL, 0, 0, 0, 0);
    rc = gldChoosePixelFormat(&pf, attribs); CHECK("pixel format", !rc && pf);
    g_gld_force_w = 640; g_gld_force_h = 480; g_H = 480;
    rc = gldCreateContext(&ctx, pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0); CHECK("create context", !rc && ctx);
    if (!ctx) return 1;
    { unsigned char *dr = calloc(1, 0x80); rc = gldAttachDrawable(ctx, 0x36, dr, 0); }
    p = MACH64_PRIV(ctx); CHECK("attach", !rc && p->attached);
    gli = calloc(1, 0x100000);
    *(uint32_t *)(gli + 0x7898) = (uint32_t)(uintptr_t)ctx;
    *(uint32_t *)((unsigned char *)ctx + 0x10) = (uint32_t)(uintptr_t)gli + 0x3610;
    { float f = 1.0f; memcpy(gli + 0x6630, &f, 4); memcpy(gli + 0x6394, &f, 4); }
    memset(gli + 0x63b4, 1, 4);
    *(uint32_t *)(gli + 0x6708) = 1; *(uint32_t *)(gli + 0x633c) = 0x0201u << 16;
    { float cc[4] = { 0.f, 0.f, 1.f, 1.f }; memcpy(gli + 0x6320, cc, 16); }
    gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)infob);
    printf("      target %ux%u, %u bits, pitch %u px, fs_scale %u, use_ovl %u, second block 0x%x\n", p->fb_w, p->fb_h, p->rb.bpp * 8, p->rb.pitch, p->fs_scale, p->use_ovl, p->rb.col_alt);
    CHECK("fs_scale selected for a 640x480 drawable on a larger scanout", p->fs_scale == 1);
    CHECK(want_ovl ? "overlay selected (default)" : "overlay NOT selected with RAGE_GL_OVERLAY=0", (int)p->use_ovl == want_ovl);
    if (p->use_ovl) CHECK("double buffered: a second colour block exists", p->rb.col_alt != 0);
    /* frame 1: blue clear + green triangle */
    { uint32_t first = p->rb.col_off;
      mach64_clear_color(ctx); tri(0xff00ff00u, 0); mach64_present(ctx); rgb_fence(&p->rb);
      if (p->use_ovl) {
          uint32_t g2, f2, e2, fl2; (void)f2; (void)e2;
          ragegl_info(rg, &g2, &f2, &e2, &fl2);
          CHECK("kernel reports the overlay enabled", (fl2 & RAGE_INFO_OVL) != 0);
          CHECK("overlay present is active in the bundle (not fallen back to the quad)", p->rb.ovl_on == 1 && p->rb.ovl_fail == 0);
          CHECK("targets swapped after the present", p->rb.col_off != first && p->rb.col_alt == first);
          printf("      front block 0x%x raw: (3,3)=%04x (630,470)=%04x (120,60)=%04x (400,60)=%04x   other block 0x%x: (3,3)=%04x (120,60)=%04x  fmt555=%d pitch=%u\n", first, rd16(&p->rb, first, 3, 3, p->rb.pitch), rd16(&p->rb, first, 630, 470, p->rb.pitch), rd16(&p->rb, first, 120, 60, p->rb.pitch), rd16(&p->rb, first, 400, 60, p->rb.pitch), p->rb.col_off, rd16(&p->rb, p->rb.col_off, 3, 3, p->rb.pitch), rd16(&p->rb, p->rb.col_off, 120, 60, p->rb.pitch), p->rb.fmt555, p->rb.pitch);
          CHECK("front (shown) block: blue background", is_blue(p, rd16(&p->rb, first, 3, 3, p->rb.pitch)) && is_blue(p, rd16(&p->rb, first, 630, 470, p->rb.pitch)));
          CHECK("front (shown) block: green triangle (120,60)", is_green(p, rd16(&p->rb, first, 120, 60, p->rb.pitch)));
          CHECK("front (shown) block: blue outside the triangle (400,60)", is_blue(p, rd16(&p->rb, first, 400, 60, p->rb.pitch)));
      } else CHECK("quad path active (no overlay)", p->rb.ovl_on == 0);
      /* frame 2: blue clear + red triangle, shifted: the other block must show it, the first must stay intact until reused */
      mach64_clear_color(ctx); tri(0xffff0000u, 200); mach64_present(ctx); rgb_fence(&p->rb);
      if (p->use_ovl) {
          CHECK("frame 2 shown from the other block: red triangle (320,60)", is_red(p, rd16(&p->rb, first == p->rb.col_off ? p->rb.col_alt : p->rb.col_alt, 320, 60, p->rb.pitch)) || is_red(p, rd16(&p->rb, p->rb.col_alt, 320, 60, p->rb.pitch)));
          CHECK("targets swapped back", p->rb.col_off == first);
      }
    }
    if (hold) { printf("READY\n"); for (;;) { mach64_clear_color(ctx); tri(0xff00ff00u, 0); mach64_present(ctx); usleep(50000); } }
    /* timing: whole frames (clear + one triangle + present) and presents only */
    { double t0, ms, ms_p; int N = 60;
      t0 = now_ms(); for (i = 0; i < N; i++) { mach64_clear_color(ctx); tri(i & 1 ? 0xff00ff00u : 0xffff0000u, 0); mach64_present(ctx); } rgb_fence(&p->rb); ms = (now_ms() - t0) / N;
      t0 = now_ms(); for (i = 0; i < N; i++) mach64_present(ctx); rgb_fence(&p->rb); ms_p = (now_ms() - t0) / N;
      printf("TIMING %s: %.2f ms per frame (clear + triangle + present), %.2f ms per present alone\n", p->use_ovl ? "overlay" : "quad", ms, ms_p);
      CHECK("frame rate sane (< 40 ms)", ms < 40.0); }
    gldDestroyContext(ctx);
    { uint32_t g2, f2, e2, fl2; ragegl_info(rg, &g2, &f2, &e2, &fl2); CHECK("the overlay is disabled again after the context is destroyed", !(fl2 & RAGE_INFO_OVL)); }
    gldDestroyPixelFormat(pf);
    printf("OVL_GATE: %d passed, %d failed\n", npass, nfail);
    return nfail ? 2 : 0;
}
