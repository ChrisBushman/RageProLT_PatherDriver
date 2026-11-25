/* tools/ragegl/ragegl_test.c - user-space acceptance test of the GL client path (kext debug build needed only for the scan-out check).
 * Every check prints PASS/FAIL; exit status = number of failures.  Hostile-argument checks are included: they must be refused. */
#include "ragegl.h"
#include <stdlib.h>
static int fails, checks;
#define CHECK(name, cond) do { checks++; if (cond) printf("PASS  %s\n", name); else { fails++; printf("FAIL  %s\n", name); } } while (0)
static uint16_t rd16(const uint8_t *b, uint32_t o) { return (uint16_t)(b[o] | (b[o + 1] << 8)); }   /* VRAM is little-endian */

int main(int argc, char **argv)
{
    ragegl_t c; uint32_t gen, feat, end, flags, off = 0, off2 = 0, i, w, h, pitch, bpp, base, done;
    uint8_t *img; kern_return_t kr; int scan = argc > 1 && !strcmp(argv[1], "scan");
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    CHECK("info", ragegl_info(c, &gen, &feat, &end, &flags) == 0 && end == 0x7ff800 && (flags & 1) && !(gen & 1));
    CHECK("display", ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base) == 0 && w >= 320 && h >= 200 && (bpp == 2 || bpp == 4));
    printf("      display %ux%u pitch %u bpp %u base 0x%x gen %u\n", w, h, pitch, bpp, base, gen);
    CHECK("alloc 512 KB", ragegl_alloc(c, 0x80000, 4096, &off) == 0 && off % 4096 == 0 && off + 0x80000 <= 0x770000);
    CHECK("alloc huge refused", ragegl_alloc(c, 0x900000, 8, &off2) != 0);
    CHECK("free of something not owned refused", ragegl_free(c, 0x1234000) != 0);
    CHECK("target on unowned span refused", ragegl_target(c, 0, 256, 2, 256, 256) != 0);
    CHECK("target overrunning the block refused", ragegl_target(c, off, 256, 2, 256, 4096) != 0);
    CHECK("target ok", ragegl_target(c, off, 256, 2, 256, 256) == 0);
    CHECK("fill whole target", ragegl_fill(c, 0, 0, 256, 256, 0x07e0) == 0);
    CHECK("scissor", ragegl_scissor(c, 64, 127, 32, 95) == 0);
    CHECK("fill larger than the scissor", ragegl_fill(c, -10, -10, 400, 400, 0xf800) == 0);
    img = malloc(256 * 256 * 2);
    CHECK("vram read", ragegl_vread(c, off, img, 256 * 256 * 2, 0) == 0);
    { int bad = 0; uint32_t x, y;
      for (y = 0; y < 256; y++) for (x = 0; x < 256; x++) {
          uint16_t want = (x >= 64 && x <= 127 && y >= 32 && y <= 95) ? 0xf800 : 0x07e0;
          if (rd16(img, (y * 256 + x) * 2) != want) bad++;
      }
      printf("      %d wrong pixels\n", bad); CHECK("fill result exact and contained", bad == 0); }
    /* upload a 64x8 pattern with byte swapping, read it back both ways */
    { uint16_t pat[64 * 8], back[64 * 8]; uint32_t k;
      for (k = 0; k < 64 * 8; k++) pat[k] = (uint16_t)(0x1234 + k);
      CHECK("vram write (swap16)", ragegl_vwrite(c, off + 0x10000, pat, sizeof pat, RAGE_XFER_SWAP16) == 0);
      CHECK("vram read (swap16) round trip", ragegl_vread(c, off + 0x10000, back, sizeof back, RAGE_XFER_SWAP16) == 0 && !memcmp(pat, back, sizeof pat));
      CHECK("vram read raw sees little-endian pixels", ragegl_vread(c, off + 0x10000, back, sizeof back, 0) == 0 && back[0] == 0x3412); }
    CHECK("vram write outside owned refused", ragegl_vwrite(c, off + 0x80000 - 4, img, 8, 0) != 0);
    CHECK("vram write unaligned refused", ragegl_vwrite(c, off + 2, img, 8, 0) != 0);
    CHECK("vram write bad flags refused", ragegl_vwrite(c, off, img, 8, 0x80) != 0);
    CHECK("vram write into the scanout refused", ragegl_vwrite(c, base, img, 8, 0) != 0);
    /* a triangle through the validated batch path */
    { uint32_t tri[2 * 40], n = 0, v; int32_t vx[3] = { 10, 250, 60 }, vy[3] = { 10, 20, 240 };
      ragegl_scissor(c, 0, 255, 0, 255);
      ragegl_fill(c, 0, 0, 256, 256, 0);
#define P(r, val) do { tri[n++] = (r); tri[n++] = (val); } while (0)
      P(0x304, 0); P(0x54c, 0); P(0x5fc, 0x100c1); P(0x6c4, 0x001f); P(0x6c8, 0xffffffffu); P(0x6d4, 0x70007); P(0x6d8, 0x101); P(0x708, 0);
      for (v = 0; v < 3; v++) { uint32_t b = 0x240 + v * 0x20;
          P(b, 0); P(b + 4, 0); P(b + 8, 0x3f800000u); P(b + 0xc, 0); P(b + 0x10, 0); P(b + 0x14, 0xffffffffu);
          P(b + 0x18, ((uint32_t)(vx[v] * 4) << 16) | ((uint32_t)(vy[v] * 4) & 0xffff)); }
      P(0x300, 0x37995b4du);
      CHECK("triangle batch", ragegl_submit(c, tri, n / 2) == 0);
      ragegl_vread(c, off, img, 256 * 256 * 2, 0);
      { uint32_t blue = 0; for (i = 0; i < 256 * 256; i++) if (rd16(img, i * 2) == 0x001f) blue++;
        printf("      %u blue pixels\n", blue); CHECK("triangle drew a plausible area (~27350)", blue > 26000 && blue < 28500); }
      { uint32_t evil[4] = { 0x6a8, 0x1fff0000u, 0x6c4, 1 };
        CHECK("batch with the scissor register refused", ragegl_submit(c, evil, 2) != 0); } }
    CHECK("fence", ragegl_fence(c, 1, &done) == 0 && done >= 1);
    /* present: copy a 100x60 region of the target to the scanout at (20,20) */
    { kern_return_t pk;
      ragegl_scissor(c, 0, 255, 0, 255);
      ragegl_fill(c, 0, 0, 256, 256, 0x07e0);
      pk = ragegl_present(c, 0, 0, 100, 60, 20, 20);
      printf("      present returned 0x%x\n", pk);
      CHECK("present ok or unsupported by format (never an error crash)", pk == 0 || pk == (kern_return_t)0xe00002c7);
      if (scan && pk == 0) {
          uint32_t row[256]; uint32_t x, y, bad = 0, inside = 0;
          uint8_t *line = malloc(pitch * bpp);
          for (y = 0; y < 100; y++) {
              if (ragegl_scanout_read(c, y * pitch * bpp, line, pitch * bpp) != 0) { bad = 99999; break; }
              for (x = 0; x < 140; x++) {
                  uint16_t v2 = rd16(line, x * 2); int in = (x >= 20 && x < 120 && y >= 20 && y < 80);
                  if (in) { inside++; if (v2 != 0x07e0) bad++; }
              }
          }
          (void)row; free(line);
          printf("      %u pixels inside the blit rect, %u wrong\n", inside, bad);
          CHECK("scan-out holds the presented rectangle", bad == 0 && inside == 6000);
      }
    }
    CHECK("present with a rect off-screen is a no-op", ragegl_present(c, 0, 0, 10, 10, 100000, 100000) == 0);
    free(img);
    ragegl_close(c);
    printf("ragegl_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
