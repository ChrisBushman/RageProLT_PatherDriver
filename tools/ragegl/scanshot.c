/* scanshot FILE.ppm: dump the Rage scanout (needs the RAGE_DEBUG_ENGTEST kext build: selector 13).  15 bpp 1:5:5:5 little-endian as the stub framebuffer sets it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rage_backend.h"
int main(int argc, char **argv)
{
    rgb_t b; uint32_t g, w, h, pitch, bpp, base, y, x; unsigned char *buf; FILE *f;
    if (argc < 2 || rgb_open(&b)) { fprintf(stderr, "usage: scanshot FILE (kext loaded?)\n"); return 1; }
    if (IOConnectMethodScalarIScalarO(b.conn, 12 /* DISPLAY */, 0, 6, &g, &w, &h, &pitch, &bpp, &base)) { fprintf(stderr, "display query failed\n"); return 1; }
    printf("scanout %ux%u pitch %u bpp %u base 0x%x\n", w, h, pitch, bpp, base);
    buf = malloc((size_t)pitch * h * bpp);
    for (y = 0; y < h; y++) {
        uint32_t off = y * pitch * bpp, n = pitch * bpp;
        if (IOConnectMethodScalarIScalarO(b.conn, 13 /* SCANOUT_READ */, 3, 0, off, (uint32_t)(uintptr_t)(buf + off), n)) { fprintf(stderr, "read failed row %u\n", y); return 1; }
    }
    f = fopen(argv[1], "wb"); fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        const unsigned char *s = buf + (y * pitch + x) * bpp; unsigned v = s[0] | (s[1] << 8), r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31; unsigned char o[3];
        o[0] = (unsigned char)((r5 << 3) | (r5 >> 2)); o[1] = (unsigned char)((g5 << 3) | (g5 >> 2)); o[2] = (unsigned char)((b5 << 3) | (b5 >> 2)); fwrite(o, 1, 3, f);
    }
    fclose(f); rgb_close(&b); return 0;
}
