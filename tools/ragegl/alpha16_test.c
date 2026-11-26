/* tools/ragegl/alpha16_test.c - what destination alpha does the engine assume for a 16 bpp target (RGB565 and ARGB1555)?  Blend SRC = DST_ALPHA / DST = 0 of a red triangle over a fresh green fill
 * (green 565 0x07e0 / 1555 0x03e0 with and without the alpha bit): the red weight equals Ad.  Private allocator block only. */
#include "ragegl.h"
#include <stdlib.h>
static ragegl_t c; static uint32_t off;
static uint32_t pr[200], np;
#define P(r, v) do { pr[np++] = (r); pr[np++] = (v); } while (0)
static void vtx(int slot, uint32_t argb, int x, int y) { uint32_t b = 0x240 + (uint32_t)slot * 0x20; P(b, 0); P(b + 4, 0); P(b + 8, 0x3f800000u); P(b + 0xc, 0); P(b + 0x10, 0); P(b + 0x14, argb); P(b + 0x18, ((uint32_t)(x * 4) << 16) | ((uint32_t)(y * 4) & 0xffffu)); }
int main(void)
{
    uint32_t k, gen; union { float f; uint32_t u; } ar; static const uint32_t sc[3] = { 0, 1, 2 };
    struct { const char *n; uint32_t fmt; uint32_t pitch; } tg[] = { { "RGB565 ", 0, 64 }, { "ARGB1555", 1, 64 } };
    ar.f = 1.0f / 1800.0f;
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    (void)sc;
    for (k = 0; k < 2; k++) {
        uint8_t b[16]; uint16_t v; uint32_t fills[2], f;
        if (ragegl_alloc(c, 64 * 64 * 2, 4096, &off) || ragegl_target(c, off, 64, 2, 64, 64)) { printf("target failed\n"); return 1; }
        ragegl_scissor(c, 0, 63, 0, 63);
        fills[0] = k ? 0x03e0 : 0x07e0; fills[1] = k ? 0x83e0 : 0x07e0;      /* 1555: the alpha bit is bit 15 (the kernel converts a 565 fill colour to 1555 only when the scanout is 15 bit: here we write the raw word via the 2D fill) */
        for (f = 0; f < 2; f++) {
            { uint32_t ga = f ? 0xff000000u : 0u;                                   /* set the destination alpha by drawing green with blend off: ALPHA_DST_SEL = As writes the vertex alpha */
              np = 0; P(0x304, 0); P(0x54c, 0); P(0x774, 0); P(0x5fc, 0x000100c1u); P(0x550, (7u << 4) | (4u << 8));
              P(0x6c8, 0xffffffffu); P(0x708, 0); P(0x6d4, 0x00070007u); P(0x6d8, 0x00000505u);
              vtx(0, ga | 0x0000ff00u, 0, 0); vtx(1, ga | 0x0000ff00u, 60, 0); vtx(2, ga | 0x0000ff00u, 0, 60); P(0x300, ar.u);
              ragegl_submit(c, pr, np / 2); ragegl_fence(c, 1, &gen); }
            np = 0; P(0x304, 0); P(0x54c, 0); P(0x774, 0); P(0x5fc, (0x000100c1u & ~(7u << 16)) | (6u << 16) | (0u << 19) | (1u << 11)); P(0x550, (7u << 4) | (4u << 8));
            P(0x6c8, 0xffffffffu); P(0x708, 0); P(0x6d4, 0x00070007u); P(0x6d8, 0x00000505u);
            vtx(0, 0xffff0000u, 0, 0); vtx(1, 0xffff0000u, 60, 0); vtx(2, 0xffff0000u, 0, 60); P(0x300, ar.u);
            ragegl_submit(c, pr, np / 2); ragegl_fence(c, 1, &gen);
            if (ragegl_vread(c, off + 10 * 64 * 2, b, 16, 1)) { printf("vread failed\n"); return 1; }
            v = (uint16_t)((b[4] << 8) | b[5]);                                                    /* SWAP16: first pixel in the high half */
            printf("%s dst alpha written %s, SRC=DST_ALPHA/DST=ZERO red triangle -> pixel %04x (%s)\n", tg[k].n, f ? "ff" : "00", v, (k ? (v >> 10) & 31 : v >> 11) > 20 ? "red weight ~1: Ad = 1" : (k ? (v >> 10) & 31 : v >> 11) < 4 ? "red weight ~0: Ad = 0" : "partial");
        }
    }
    ragegl_close(c); return 0;
}
