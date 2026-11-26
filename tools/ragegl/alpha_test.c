/* tools/ragegl/alpha_test.c - what does the 3D engine write into the ALPHA byte of a 32 bpp (ARGB8888) target?  Everything happens inside a private allocator block (no scanout writes,
 * works at any display mode).  Survey: 2D fill, flat triangles (blend off / on / write-mask), the ALPHA_TST_CNTL ALPHA_DST_SEL field, a textured quad from an ARGB8888 texture.
 * Prints the measured table; the CHECKs encode what the survey established. */
#include "ragegl.h"
#include <stdlib.h>
static int fails, checks;
#define CHECK(name, cond) do { checks++; if (cond) printf("PASS  %s\n", name); else { fails++; printf("FAIL  %s\n", name); } } while (0)
static ragegl_t c;
#define N 64u
static uint32_t rd32(const uint8_t *b, uint32_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
static uint32_t off;
static uint32_t pix(uint32_t x, uint32_t y) { uint8_t b[8]; uint32_t d; ragegl_fence(c, 1, &d); if (ragegl_vread(c, off + (y * N + x) * 4, b, 8, 0)) return 0xdeadbeefu; return rd32(b, 0); }

static uint32_t pr[200]; static uint32_t np;
#define P(r, v) do { pr[np++] = (r); pr[np++] = (v); } while (0)
static void vtx(int slot, uint32_t argb, int x, int y, uint32_t s, uint32_t t)
{
    uint32_t b = 0x240 + (uint32_t)slot * 0x20;
    P(b, s); P(b + 4, t); P(b + 8, 0x3f800000u); P(b + 0xc, 0); P(b + 0x10, 0); P(b + 0x14, argb); P(b + 0x18, ((uint32_t)(x * 4) << 16) | ((uint32_t)(y * 4) & 0xffffu));
}
/* flat (or gouraud-constant) triangle covering the target's upper-left half: (0,0) (60,0) (0,60); ooa = 1/area as float bits: area 1800 -> 16/cross, cross = 16*1800 */
static uint32_t area_bits(void) { union { float f; uint32_t u; } v; v.f = 1.0f / 1800.0f; return v.u; }
static void tri(uint32_t scale3d, uint32_t atc, uint32_t wmask, uint32_t argb)
{
    np = 0;
    P(0x304, 0); P(0x54c, 0); P(0x774, 0); P(0x5fc, scale3d); P(0x550, atc); P(0x6c8, wmask); P(0x708, 0); P(0x6d4, 0x00070007u); P(0x6d8, 0x00000505u);
    vtx(0, argb, 0, 0, 0, 0); vtx(1, argb, 60, 0, 0, 0); vtx(2, argb, 0, 60, 0, 0);
    P(0x300, area_bits());
    ragegl_submit(c, pr, np / 2);
}
int main(void)
{
    uint32_t gen, w, h, pitch, bpp, base, k, v, a;
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base);
    if (ragegl_alloc(c, N * N * 4, 4096, &off) || ragegl_target(c, off, N, 4, N, N)) { printf("target failed\n"); return 1; }
    ragegl_scissor(c, 0, N - 1, 0, N - 1);
    /* 2D fill keeps the colour's alpha exactly */
    ragegl_fill(c, 0, 0, N, N, 0x80102030u); v = pix(40, 40);
    printf("2D fill 0x80102030            -> 0x%08x\n", v); CHECK("2D fill writes the alpha byte as given", v == 0x80102030u);
    /* flat triangle, blend off (SRC_ONE/DST_ZERO), default ALPHA_DST_SEL = As: dst alpha vs vertex alpha */
    for (k = 0; k < 5; k++) {
        static const uint32_t al[5] = { 0x00, 0x40, 0x80, 0xc0, 0xff };
        a = al[k]; ragegl_fill(c, 0, 0, N, N, 0x11204080u);
        tri(0x000100c1u, (7u << 4) | (4u << 8), 0xffffffffu, (a << 24) | 0x00ff8040u); v = pix(10, 10);
        printf("tri blend off, vertex alpha %02x -> 0x%08x  (alpha %02x)\n", a, v, v >> 24);
        if (a == 0x80) CHECK("opaque-blend triangle: dst alpha = source alpha (ALPHA_DST_SEL = As)", (v >> 24) == 0x80 && (v & 0xffffff) == 0xff8040);
    }
    /* blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA on a destination of alpha 0x11 */
    for (k = 0; k < 3; k++) {
        static const uint32_t al[3] = { 0x40, 0x80, 0xc0 };
        a = al[k]; ragegl_fill(c, 0, 0, N, N, 0x11204080u);
        tri((0x000100c1u & ~(7u << 16)) | (4u << 16) | (5u << 19) | (1u << 11), (7u << 4) | (4u << 8), 0xffffffffu, (a << 24) | 0x00ff8040u); v = pix(10, 10);
        printf("tri blend SRCA/1-SRCA, vertex alpha %02x over dst 0x11204080 -> 0x%08x (alpha %02x)\n", a, v, v >> 24);
    }
    /* colour mask without alpha: alpha byte must stay */
    ragegl_fill(c, 0, 0, N, N, 0x11204080u); tri(0x000100c1u, (7u << 4) | (4u << 8), 0x00ffffffu, 0xff00ff00u); v = pix(10, 10);
    printf("tri with DP_WRITE_MASK 00ffffff (alpha protected) -> 0x%08x\n", v); CHECK("write mask without the alpha bits leaves the alpha byte alone", (v >> 24) == 0x11 && (v & 0xffffff) == 0x00ff00);
    /* DP_WRITE_MASK alpha only */
    ragegl_fill(c, 0, 0, N, N, 0x11204080u); tri(0x000100c1u, (7u << 4) | (4u << 8), 0xff000000u, 0xcc00ff00u); v = pix(10, 10);
    printf("tri with DP_WRITE_MASK ff000000 (alpha only) -> 0x%08x\n", v); CHECK("alpha-only write mask changes only the alpha byte", (v & 0xffffff) == 0x204080 && (v >> 24) == 0xcc);
    /* ALPHA_DST_SEL */
    { static const uint32_t sel[] = { 0, 1, 4, 5, 6, 7 }; static const char *nm[] = { "0x00", "0xff", "As", "1-As", "Ad", "1-Ad" };
      for (k = 0; k < 6; k++) { ragegl_fill(c, 0, 0, N, N, 0x40204080u); tri(0x000100c1u, (7u << 4) | (sel[k] << 8), 0xffffffffu, 0x8000ff00u); v = pix(10, 10);
        printf("ALPHA_DST_SEL %u (%-4s): vertex alpha 80, dst alpha 40 -> alpha %02x\n", sel[k], nm[k], v >> 24); } }
    /* is the destination read fresh after a 2D fill?  fill a NEW colour/alpha each time, draw with blending, and see which destination the engine used */
    { static const uint32_t fillc[3] = { 0x40a0a0a0u, 0xc0301030u, 0x70e0c0a0u }; const uint32_t bl = (0x000100c1u & ~(7u << 16)) | (4u << 16) | (5u << 19) | (1u << 11);
      for (k = 0; k < 3; k++) {
          uint32_t fc = fillc[k], fa = fc >> 24, e; int ok;
          ragegl_fill(c, 0, 0, N, N, fc); tri(bl, (7u << 4) | (4u << 8), 0xffffffffu, 0x80ff0000u); v = pix(10, 10);          /* src red, alpha 80 -> 50% */
          e = (((0xffu * 128u + ((fc >> 16) & 255u) * 127u) / 255u) << 16);                                        /* expected red channel (50% of ff + 50% of dst red) */
          ok = (int)((v >> 16) & 255) - (int)(e >> 16) < 4 && (int)(e >> 16) - (int)((v >> 16) & 255) < 4;
          printf("blend over fresh fill %08x: result %08x (red %s: expected ~%02x)\n", fc, v, ok ? "from the NEW destination" : "from a STALE destination", e >> 16);
          if (k == 0) CHECK("blending reads the destination written by the preceding 2D fill", ok);
          (void)fa; }
      for (k = 0; k < 3; k++) {                                                                                  /* ALPHA_DST_SEL = Ad / 1-Ad with the destination fetched (blend on) */
          uint32_t fc = fillc[k];
          ragegl_fill(c, 0, 0, N, N, fc); tri(bl, (7u << 4) | (6u << 8), 0xffffffffu, 0x80ff0000u); v = pix(10, 10);
          printf("blend on, ALPHA_DST_SEL = Ad, dst alpha %02x -> alpha %02x\n", fc >> 24, v >> 24);
          if (k == 0) CHECK("ALPHA_DST_SEL = Ad keeps the destination alpha (blend on)", (v >> 24) == (fc >> 24)); }
      { uint32_t fc = 0x40a0a0a0u;                                                                                /* DST_ALPHA as the SOURCE factor: weight = Ad (0x40 -> 25%) */
        ragegl_fill(c, 0, 0, N, N, fc);
        tri((0x000100c1u & ~(7u << 16)) | (6u << 16) | (0u << 19) | (1u << 11), (7u << 4) | (4u << 8), 0xffffffffu, 0xffff0000u); v = pix(10, 10);
        printf("blend DST_ALPHA/ZERO over %08x: result %08x (red expected ~%02x)\n", fc, v, 0xffu * 0x40u / 255u);
        CHECK("DST_ALPHA blend factor uses the destination's alpha byte", (int)((v >> 16) & 255) - (int)(0xffu * 0x40u / 255u) < 5 && (int)(0xffu * 0x40u / 255u) - (int)((v >> 16) & 255) < 5); } }
    /* textured quad from an ARGB8888 texture: texel alpha 0x40; vertex alpha 0xff; TEX_MAP_AEN in SCALE_3D (bit 30) */
    { uint32_t toff, tex[16 * 16], t, d; uint8_t *tb = (uint8_t *)tex;
      for (t = 0; t < 256; t++) tex[t] = 0x40ff8000u;                            /* alpha 40, R ff, G 80, B 00 */
      (void)tb;
      if (ragegl_alloc(c, 4096, 4096, &toff) == 0 && ragegl_vwrite(c, toff, tex, sizeof tex, RAGE_XFER_SWAP32) == 0 && ragegl_texture(c, 4, toff, 6) == 0) {
          for (k = 0; k < 2; k++) {
              ragegl_fill(c, 0, 0, N, N, 0x11204080u); np = 0;
              P(0x304, 0); P(0x54c, 0); P(0x770, 4u | (4u << 4) | (4u << 8)); P(0x774, (1u << 9) | (1u << 30)); P(0x774, (1u << 9) | (1u << 23) | (1u << 30));
              P(0x5fc, (k ? ((0x4141009bu & ~(7u << 16)) | (4u << 16) | (5u << 19) | (1u << 11)) : 0x4141009bu)); P(0x550, (7u << 4) | (4u << 8)); P(0x6c8, 0xffffffffu); P(0x708, 0); P(0x6d4, 0x00070007u); P(0x6d8, 0x00000505u);
              { union { float f; uint32_t u; } hi, lo; hi.f = 1.0f; lo.f = 0.0f;
                vtx(0, 0xffffffffu, 0, 0, lo.u, lo.u); vtx(1, 0xffffffffu, 60, 0, hi.u, lo.u); vtx(2, 0xffffffffu, 0, 60, lo.u, hi.u); }
              P(0x300, area_bits()); ragegl_submit(c, pr, np / 2); ragegl_fence(c, 1, &d); v = pix(10, 10);
              printf("textured (ARGB8888 texel 40ff8000, vertex white), blend %s -> 0x%08x (alpha %02x)\n", k ? "SRCA/1-SRCA over 11204080" : "off", v, v >> 24);
          }
      } else printf("texture setup failed\n"); }
    printf("alpha_test: %d checks, %d failed\n", checks, fails);
    ragegl_close(c);
    return fails;
}
