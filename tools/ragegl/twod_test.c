/* tools/ragegl/twod_test.c - B1b gate: drives the type-2 2D context client directly with command streams built exactly as the stock ATIRageProGA plugin builds them
 * (docs/re-notes/ga-2d-stream-spec.md section 4), and checks the pixels on the scanout through the GL client's debug scanout read (DEBUG kext: tools/rig/redeploy.sh).
 *   twod_test            all checks
 * Needs the Rage display up (stub framebuffer, a mode set).  Draws only inside a 256x128 area near (64,64) of the Rage scanout and restores nothing: run it on an idle display. */
#include "ragegl.h"
#include <stdlib.h>

#define RAGE_2D_TYPE 2
static ragegl_t gl; static io_connect_t c2;
static uint32_t gen, W, H, PITCH, BPP, BASE;
static int fails, checks;
#define CHECK(name, cond) do { checks++; if (!(cond)) { fails++; printf("FAIL  %s\n", name); } else printf("pass  %s\n", name); } while (0)

static uint32_t *buf; static uint32_t nw; static uint32_t hdr_at;
static int map_buffer(void)                        /* each map is the doorbell: it submits the previous buffer and returns a fresh one */
{
    vm_address_t a = 0; vm_size_t sz = 0; kern_return_t kr = IOConnectMapMemory(c2, 0, mach_task_self(), &a, &sz, kIOMapAnywhere);
    if (kr != KERN_SUCCESS || !a || sz < 4096) { printf("map failed %x sz %lu\n", kr, (unsigned long)sz); return -1; }
    buf = (uint32_t *)a; nw = 7; buf[7] = 0;       /* the plugin zeroes the initial header word at +0x1c, body from +0x20 */
    return 0;
}
static void begin(void) { nw = 7; hdr_at = nw; buf[nw++] = 0; }
static void P(uint32_t r, uint32_t v) { buf[nw++] = r; buf[nw++] = v; }
static void hdr(uint32_t op) { hdr_at = nw; buf[nw++] = op << 24; }
static void word(uint32_t v) { buf[nw++] = v; }
static void endpkt(void) { buf[hdr_at] |= (nw - hdr_at); }
static void term(void) { buf[nw++] = 0x01000000u; }
static uint32_t YX(int x, int y) { return ((uint32_t)x << 16) | ((uint32_t)y & 0xffffu); }
static void submit(void) { endpkt(); term(); if (map_buffer()) exit(2); IOConnectMethodScalarIStructureI(c2, 7, 1, 0, 0, (void *)0); }   /* doorbell, then finish(mode 0) */

static void fill(int x, int y, int w, int h, uint32_t col)
{
    begin(); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, col); P(0x10c, YX(x, y)); P(0x118, ((uint32_t)w << 16) | (uint32_t)h); submit();
}
static void copy(int sx, int sy, int dx, int dy, int w, int h)
{
    uint32_t cn = 3;
    begin(); hdr_at = nw - 1; buf[hdr_at] = 0x0d000000u; word(0); P(0x1b4, 0); P(0x2d8, 0x300); P(0x2d4, 0x70003);
    if ((uint32_t)dx > (uint32_t)sx) { sx += w - 1; dx += w - 1; cn &= ~1u; }
    if ((uint32_t)dy > (uint32_t)sy) { sy += h - 1; dy += h - 1; cn &= ~2u; }
    P(0x130, cn); P(0x18c, YX(sx, sy)); P(0x190, (uint32_t)w); P(0x10c, YX(dx, dy)); P(0x118, ((uint32_t)w << 16) | (uint32_t)h); submit();
}
static uint8_t *snap(int x, int y, int w, int h)   /* w*h pixels, row by row, BPP bytes each */
{
    uint8_t *m = (uint8_t *)malloc((size_t)w * h * BPP); int r;
    for (r = 0; r < h; r++) if (ragegl_scanout_read(gl, ((uint32_t)(y + r) * PITCH + (uint32_t)x) * BPP, m + (size_t)r * w * BPP, (uint32_t)w * BPP)) { printf("scanout read failed\n"); exit(2); }
    return m;
}
static uint32_t px(const uint8_t *m, int w, int x, int y) { const uint8_t *p = m + ((size_t)y * w + x) * BPP; return BPP == 4 ? ((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) : ((uint32_t)p[0] << 8 | p[1]); }

int main(void)
{
    mach_port_t master; io_service_t svc; kern_return_t kr; uint32_t a = 0, b = 0, base0, o0[12];
    uint32_t colA, colB, colC, mask; uint8_t *s0, *s1; int x, y, bad;
    if (ragegl_open(&gl)) { printf("no GL client (debug kext?)\n"); return 2; }
    if (ragegl_display(gl, &gen, &W, &H, &PITCH, &BPP, &base0)) { printf("display query failed\n"); return 2; }
    BASE = base0; printf("display %ux%u pitch %u bpp(bytes) %u base 0x%x\n", W, H, PITCH, BPP, BASE);
    if (BPP != 2 && BPP != 4) { printf("unsupported depth\n"); return 2; }
    IOMasterPort(MACH_PORT_NULL, &master);
    svc = IOServiceGetMatchingService(master, IOServiceMatching("RageLTDevice"));
    kr = IOServiceOpen(svc, mach_task_self(), RAGE_2D_TYPE, &c2);
    CHECK("type-2 client opens", kr == KERN_SUCCESS);
    if (kr) return 1;
    CHECK("type-3 client refused", ({ io_connect_t t; IOServiceOpen(svc, mach_task_self(), 3, &t) != KERN_SUCCESS; }));
    CHECK("get_config = (0x04000000, 0)", IOConnectMethodScalarIScalarO(c2, 1, 0, 2, &a, &b) == KERN_SUCCESS && a == 0x04000000u && b == 0);
    { uint32_t out[12]; size_t osz = sizeof out; kr = IOConnectMethodScalarIStructureO(c2, 0, 2, &osz, 0, 0, out);
      CHECK("set_surface(display 0) returns 0x30 bytes with the display size", kr == KERN_SUCCESS && osz == 0x30 && out[2] == W && out[3] == H); }
    { uint32_t out[12]; size_t osz = sizeof out; CHECK("set_surface(display 1) refused", IOConnectMethodScalarIStructureO(c2, 0, 2, &osz, 1, 0, out) != KERN_SUCCESS); }
    CHECK("selector 16 (raw register window) does not exist", IOConnectMethodScalarIStructureI(c2, 16, 0, 0, (void *)0, 0) != KERN_SUCCESS);
    (void)o0;
    if (map_buffer()) return 2;
    mask = BPP == 4 ? 0xffffffffu : 0xffffu;
    colA = BPP == 4 ? 0x00ff2010u : 0x7c1fu; colB = BPP == 4 ? 0x0010a0f0u : 0x03e0u; colC = BPP == 4 ? 0x00f0f010u : 0xffe0u;

    /* 1. solid fill inside a quiet area; neighbours must not change */
    fill(40, 40, 200, 120, 0x00808080u & mask);       /* known background, so every run starts from the same pixels */
    s0 = snap(40, 40, 200, 120);
    fill(64, 64, 60, 40, colA);
    s1 = snap(40, 40, 200, 120); bad = 0;
    for (y = 0; y < 120; y++) for (x = 0; x < 200; x++) {
        int in = (x >= 24 && x < 84 && y >= 24 && y < 64);
        if (in ? (px(s1, 200, x, y) != px(s1, 200, 24, 24)) : (px(s1, 200, x, y) != px(s0, 200, x, y))) bad++;
    }
    CHECK("fill: rectangle uniform and nothing outside it changed", bad == 0 && px(s1, 200, 24, 24) != px(s0, 200, 24, 24));
    { uint32_t seen = px(s1, 200, 30, 30); printf("      fill colour as read back: 0x%x (sent 0x%x)\n", seen, colA & mask); }
    free(s0); free(s1);

    /* 2. clipping: a fill hanging off the left/top edge must paint only the visible part and never wrap */
    fill(0, 0, 100, 60, 0x00404040u & mask);
    s0 = snap(0, 0, 100, 60);
    fill(-30, -20, 60, 50, colB);
    s1 = snap(0, 0, 100, 60); bad = 0;
    for (y = 0; y < 60; y++) for (x = 0; x < 100; x++) {
        int in = (x < 30 && y < 30);
        if (in ? (px(s1, 100, x, y) == px(s0, 100, x, y)) : (px(s1, 100, x, y) != px(s0, 100, x, y))) bad++;
    }
    CHECK("fill hanging off the top-left corner is clipped exactly to the display", bad == 0);
    free(s0); free(s1);

    /* 3. copy forward (no overlap) */
    fill(80, 100, 40, 20, colA); fill(80, 100, 20, 20, colB);         /* a two-colour source block */
    s0 = snap(40, 40, 200, 120);
    copy(80, 100, 150, 100, 40, 20);
    s1 = snap(40, 40, 200, 120); bad = 0;
    for (y = 0; y < 20; y++) for (x = 0; x < 40; x++) if (px(s1, 200, 110 + x, 60 + y) != px(s0, 200, 40 + x, 60 + y)) bad++;
    CHECK("copy: disjoint blocks, destination equals source", bad == 0);
    free(s0); free(s1);

    /* 4. copy with overlap, destination to the right and below the source (reversed walk on both axes) */
    s0 = snap(40, 40, 200, 120);
    copy(80, 100, 90, 105, 40, 20);
    s1 = snap(40, 40, 200, 120); bad = 0;
    for (y = 0; y < 20; y++) for (x = 0; x < 40; x++) if (px(s1, 200, 50 + x, 65 + y) != px(s0, 200, 40 + x, 60 + y)) bad++;   /* dest (90,105) = snapshot (50,65); source (80,100) = snapshot (40,60) */
    CHECK("copy: overlapping, destination right+below the source (memmove semantics)", bad == 0);
    free(s0); free(s1);
    /* 5. overlap forward */
    s0 = snap(40, 40, 200, 120);
    copy(100, 105, 90, 100, 40, 20);
    s1 = snap(40, 40, 200, 120); bad = 0;
    for (y = 0; y < 20; y++) for (x = 0; x < 40; x++) if (px(s1, 200, 50 + x, 60 + y) != px(s0, 200, 60 + x, 65 + y)) bad++;
    CHECK("copy: overlapping, destination left+above the source (memmove semantics)", bad == 0);
    free(s0); free(s1);
    /* 6. copy whose source reaches past the right edge: the part past the edge must not be read or written */
    s0 = snap(W - 50, 70, 50, 10);
    copy((int)W - 20, 70, (int)W - 60, 70, 60, 10);
    s1 = snap(W - 50, 70, 50, 10); bad = 0;
    for (y = 0; y < 10; y++) for (x = 0; x < 10; x++) if (px(s1, 50, x, y) != px(s0, 50, 40 + x, y)) bad++;   /* screen W-60+k <- W-20+k, visible for k = 10..19: snapshot x 0..9 <- 40..49 */
    CHECK("copy: source past the right edge is clamped (visible part copied, nothing read beyond)", bad == 0);
    free(s0); free(s1);

    /* 7. refusals: a stream writing a kernel-owned register must change nothing */
    s0 = snap(40, 40, 200, 120);
    begin(); P(0x100, 0x00400000u); P(0x2d8, 0x100); P(0x2c4, colC); P(0x10c, YX(60, 60)); P(0x118, (50u << 16) | 30u); submit();
    s1 = snap(40, 40, 200, 120);
    CHECK("refused stream (DST_OFF_PITCH write) changes no pixel", memcmp(s0, s1, 200 * 120 * BPP) == 0);
    free(s0); free(s1);
    fill(100, 40, 8, 8, colC);                                         /* the client keeps working after a refusal */
    s1 = snap(100, 40, 8, 8);
    CHECK("client still works after a refused stream", px(s1, 8, 3, 3) == px(s1, 8, 4, 4) && px(s1, 8, 0, 0) == px(s1, 8, 7, 7));
    free(s1);

    /* 8. a line (diagnostic: lines are the least certain primitive) */
    fill(40, 40, 200, 120, 0x00202020u & mask);
    s0 = snap(40, 40, 200, 120);
    { uint32_t major = 80, minor = 20; begin(); P(0x2d8, 0x100); P(0x2c4, colC); P(0x130, 0x803); P(0x10c, YX(60, 120)); P(0x124, 2 * minor - major); P(0x128, 2 * minor); P(0x12c, (uint32_t)(2 * ((int)minor - (int)major))); P(0x120, major + 1); submit(); }
    s1 = snap(40, 40, 200, 120); bad = 0;
    for (y = 0; y < 120; y++) for (x = 0; x < 200; x++) if (px(s1, 200, x, y) != px(s0, 200, x, y)) bad++;
    printf("      line: %d pixels changed (expected about 81)\n", bad);
    CHECK("line: draws between 70 and 100 pixels", bad >= 70 && bad <= 100);
    free(s0); free(s1);

    IOServiceClose(c2); ragegl_close(gl);
    printf("twod_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
