/* tools/ragegl/fb_layout.c - gate for the frame buffer layout (release kext): the kernel must keep everything below the frame buffer base (the hardware-cursor image of Apple's
 * NDRV layout lives at 0x40..0x440) and the frame buffer itself away from client allocations.  Allocates many small blocks of every size class and checks none overlaps
 * [0, base + frame size).  docs/re-notes/apple-ndrv-framebuffer.md D1-D3. */
#include <stdio.h>
#include "ragegl.h"
int main(void)
{
    ragegl_t c; uint32_t gen, w, h, pitch, bpp, base, fbend, off[64], n = 0, i, bad = 0, lowest = 0xffffffffu;
    static const uint32_t sizes[] = { 8, 16, 64, 256, 512, 1024, 2048, 4096 };
    if (ragegl_open(&c)) { printf("cannot open\n"); return 99; }
    ragegl_display(c, &gen, &w, &h, &pitch, &bpp, &base);
    fbend = base + pitch * h * bpp;
    printf("display %ux%u bpp %u pitch %u px, frame buffer 0x%x..0x%x (%s)\n", w, h, bpp, pitch, base, fbend, base ? "NDRV-style layout: block below the frame buffer" : "frame buffer at VRAM 0");
    for (i = 0; i < 8 && n < 64; i++) { uint32_t k; for (k = 0; k < 8 && n < 64; k++) { uint32_t o; if (ragegl_alloc(c, sizes[i], 8, &o)) break; off[n++] = o; if (o < lowest) lowest = o;
        if (o < fbend && o + sizes[i] > 0) { if (o + sizes[i] > 0 && (o < base || o < fbend)) { printf("  FAIL  block %u bytes at 0x%x overlaps the frame buffer area [0,0x%x)\n", sizes[i], o, fbend); bad++; } } } }
    printf("%u blocks allocated, lowest offset 0x%x\n", n, lowest);
    if (lowest < fbend) { printf("  FAIL  an allocation lies below the end of the frame buffer\n"); bad++; }
    else printf("  PASS  no allocation below 0x%x (frame buffer end)\n", fbend);
    for (i = 0; i < n; i++) ragegl_free(c, off[i]);
    printf("FB_LAYOUT: %s\n", bad ? "FAILED" : "passed");
    return bad ? 2 : 0;
}
