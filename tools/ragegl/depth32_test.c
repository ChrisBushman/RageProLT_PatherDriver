/* tools/ragegl/depth32_test.c - 32 bpp render target acceptance test (kext debug build; the scanout must be 32 bpp: switch the Rage display with cgmode ... 32 first).
 * Fill / scissor / Z clear / 3D triangle on an ARGB8888 target, same-format present (read back from the scanout), and the refused 16 bpp -> 32 bpp present. */
#include "ragegl.h"
#include <stdlib.h>
static int fails, checks;
#define CHECK(name, cond) do { checks++; if (cond) printf("PASS  %s\n", name); else { fails++; printf("FAIL  %s\n", name); } } while (0)
static uint32_t rd32(const uint8_t *b, uint32_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }   /* VRAM is little-endian */

int main(void)
{
    ragegl_t c; uint32_t gen, w, h, pitch, bpp, base, off = 0, zoff = 0, x, y, bad, n; uint8_t *img;
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    CHECK("display", ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base) == 0);
    printf("      display %ux%u pitch %u bpp %u base 0x%x gen %u\n", w, h, pitch, bpp, base, gen);
    if (bpp != 4) { printf("SKIP: the scanout is not 32 bpp\n"); return 77; }
    CHECK("alloc target+Z", ragegl_alloc(c, 0x180000, 4096, &off) == 0 && ragegl_alloc(c, 0x20000, 4096, &zoff) == 0);
    CHECK("32 bpp target accepted", ragegl_target(c, off, 256, 4, 256, 256) == 0);
    CHECK("Z block", ragegl_setz(c, zoff, 256) == 0);
    CHECK("fill whole target ARGB", ragegl_fill(c, 0, 0, 256, 256, 0xff102030u) == 0);
    CHECK("scissor", ragegl_scissor(c, 64, 127, 32, 95) == 0);
    CHECK("fill larger than the scissor", ragegl_fill(c, -10, -10, 400, 400, 0xff8040c0u) == 0);
    img = malloc(256 * 256 * 4);
    CHECK("vram read", ragegl_vread(c, off, img, 256 * 256 * 4, 0) == 0);
    for (bad = 0, y = 0; y < 256; y++) for (x = 0; x < 256; x++) {
        uint32_t want = (x >= 64 && x <= 127 && y >= 32 && y <= 95) ? 0xff8040c0u : 0xff102030u;
        if (rd32(img, (y * 256 + x) * 4) != want) { if (bad < 4) printf("      (%u,%u) = %08x want %08x\n", x, y, rd32(img, (y * 256 + x) * 4), want); bad++; }
    }
    printf("      %u wrong pixels\n", bad); CHECK("fill exact and contained", bad == 0);
    CHECK("Z clear with a 32 bpp target", RGL_CALL(RAGE_GL_CLEAR_Z, 1, 0, 0xffffu) == 0);
    { uint8_t zb[16]; CHECK("Z block cleared", ragegl_vread(c, zoff, zb, 16, 0) == 0 && rd32(zb, 0) == 0xffffffffu && rd32(zb, 12) == 0xffffffffu); }
    /* triangle through the validated batch path, 3D engine writing ARGB8888 */
    { uint32_t tri[2 * 40], v, blue = 0; int32_t vx[3] = { 10, 250, 60 }, vy[3] = { 10, 20, 240 }; n = 0;
      ragegl_scissor(c, 0, 255, 0, 255);
      ragegl_fill(c, 0, 0, 256, 256, 0xff000000u);
#define P(r, val) do { tri[n++] = (r); tri[n++] = (val); } while (0)
      P(0x304, 0); P(0x54c, 0); P(0x5fc, 0x100c1); P(0x6c4, 0x000000ffu); P(0x6c8, 0xffffffffu); P(0x6d4, 0x70007); P(0x6d8, 0x101); P(0x708, 0);
      for (v = 0; v < 3; v++) { uint32_t b = 0x240 + v * 0x20;
          P(b, 0); P(b + 4, 0); P(b + 8, 0x3f800000u); P(b + 0xc, 0); P(b + 0x10, 0); P(b + 0x14, 0xffffffffu);
          P(b + 0x18, ((uint32_t)(vx[v] * 4) << 16) | ((uint32_t)(vy[v] * 4) & 0xffff)); }
      P(0x300, 0x37995b4du);
      CHECK("triangle batch", ragegl_submit(c, tri, n / 2) == 0);
      ragegl_vread(c, off, img, 256 * 256 * 4, 0);
      for (x = 0; x < 256 * 256; x++) if ((rd32(img, x * 4) & 0xffffffu) == 0x0000ffu) blue++;
      printf("      %u blue pixels\n", blue); CHECK("triangle drew a plausible area (~27350)", blue > 26000 && blue < 28500); }
    /* same-format present: 100x60 of the target to the scanout at (20,20) */
    { uint8_t *line = malloc(pitch * 4); uint32_t inside = 0; int rd_fail = 0; kern_return_t pk;
      ragegl_scissor(c, 0, 255, 0, 255);
      ragegl_fill(c, 0, 0, 256, 256, 0xff00ff00u);
      pk = ragegl_present(c, 0, 0, 100, 60, 20, 20);
      CHECK("32 -> 32 present ok", pk == 0);
      ragegl_fence(c, 1, &gen);
      for (bad = 0, y = 0; y < 100 && !rd_fail; y++) {
          if (ragegl_scanout_read(c, y * pitch * 4, line, pitch * 4) != 0) { rd_fail = 1; break; }
          for (x = 0; x < 140; x++) if (x >= 20 && x < 120 && y >= 20 && y < 80) { inside++; if (rd32(line, x * 4) != 0xff00ff00u) bad++; }
      }
      printf("      %u pixels inside the blit rect, %u wrong\n", inside, bad);
      CHECK("scan-out holds the presented rectangle", !rd_fail && bad == 0 && inside == 6000); free(line); }
    /* a 16 bpp target cannot be presented to a 32 bpp scanout (that is the textured-quad step) */
    CHECK("16 bpp target accepted", ragegl_target(c, off, 256, 2, 256, 256) == 0);
    CHECK("16 -> 32 present with a non-power-of-two pitch refused (unsupported)", ragegl_target(c, off, 200, 2, 200, 200) == 0 && ragegl_present(c, 0, 0, 10, 10, 0, 0) == (kern_return_t)0xe00002c7);
    CHECK("16 bpp target, power-of-two pitch", ragegl_target(c, off, 256, 2, 256, 256) == 0);
    CHECK("present with a 32 bpp target and rect off-screen is a no-op", ragegl_target(c, off, 256, 4, 256, 256) == 0 && ragegl_present(c, 0, 0, 10, 10, 100000, 100000) == 0);
    free(img); ragegl_close(c);
    printf("depth32_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
