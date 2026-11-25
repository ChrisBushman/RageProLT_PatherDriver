/* tools/ragegl/gldcore_test.c - hardware test of the new bundle's backend (gldriver/rage_backend.c) driving the kernel GL client with the proven
 * packet emitter: gouraud, depth test, textured (RGB565), scissor, present.  Pixels are read back through the kernel (VRAM read), never mapped.
 * Build/run on the Panther host: make -C tools/ragegl gldcore_test; ./gldcore_test */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rage_backend.h"
#include "mach64_regs.h"

static int fails, checks;
#define CHECK(name, cond) do { checks++; if (cond) printf("PASS  %s\n", name); else { fails++; printf("FAIL  %s\n", name); } } while (0)
#define W 128
#define H 128
static uint32_t cbstore[8192];

static uint16_t px(const uint8_t *img, int x, int y) { const uint8_t *p = img + ((uint32_t)y * W + (uint32_t)x) * 2u; return (uint16_t)(p[0] | (p[1] << 8)); }
static int r5(uint16_t c) { return (c >> 11) & 31; }
static int g6(uint16_t c) { return (c >> 5) & 63; }
static int b5(uint16_t c) { return c & 31; }
static int absd(int a, int b) { return a > b ? a - b : b - a; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static void base_state(m64_state *s)
{
    memset(s, 0, sizeof *s);
    s->scale_3d_cntl = MACH64_SCALE_3D_CNTL_SOLID; s->setup_cntl = 0; s->dp_pix_width = 0x60040464u; s->dp_write_mask = 0xffffffffu;
    s->dp_mix = MACH64_DP_MIX_SS; s->dp_src = MACH64_DP_SRC_3D; s->alpha_tst_cntl = (7u << 4) | (4u << 8);
}
static void vtx(m64_vertex *v, int x, int y, uint32_t z16, uint32_t argb, float s, float t)
{
    memset(v, 0, sizeof *v);
    v->x_y = (int32_t)((((uint32_t)(x * 4)) & 0xffffu) << 16 | (((uint32_t)(y * 4)) & 0xffffu));
    v->z = (int32_t)(z16 << 15); v->w = (int32_t)fbits(1.0f); v->argb = argb; v->s = (int32_t)fbits(s); v->t = (int32_t)fbits(t);
}
static int readimg(rgb_t *b, uint8_t *img) { return rgb_readback(b, b->col_off, img, W * H * 2); }

int main(void)
{
    rgb_t b; m64_cmdbuf cb; m64_state st; m64_vertex v0, v1, v2, v3; uint8_t *img = malloc(W * H * 2); uint16_t c; int i, rc;
    if (rgb_open(&b)) { printf("cannot open the backend\n"); return 99; }
    CHECK("surface with Z", rgb_surface(&b, W, H, 2, 1) == 0);
    /* ---- 1. gouraud ---- */
    rgb_fill(&b, 0, 0, W, H, 0);
    base_state(&st);
    m64_cb_init(&cb, cbstore, 8192); m64_emit_state(&cb, &st);
    vtx(&v0, 10, 10, 0, 0xffff0000u, 0, 0); vtx(&v1, 120, 10, 0, 0xff00ff00u, 0, 0); vtx(&v2, 10, 120, 0, 0xff0000ffu, 0, 0);
    CHECK("emit gouraud triangle", m64_emit_triangle(&cb, &v0, &v1, &v2) == 1);
    rc = rgb_flush(&b, &cb); CHECK("flush gouraud", rc == 0);
    CHECK("fence", rgb_fence(&b) == 0); readimg(&b, img);
    c = px(img, 14, 14); printf("      near red vertex (14,14): r%d g%d b%d\n", r5(c), g6(c), b5(c)); CHECK("red dominates near the red vertex", r5(c) > 20 && g6(c) < 14 && b5(c) < 8);
    c = px(img, 110, 14); CHECK("green dominates near the green vertex", g6(c) > 40 && r5(c) < 8 && b5(c) < 8);
    c = px(img, 14, 110); CHECK("blue dominates near the blue vertex", b5(c) > 20 && r5(c) < 8 && g6(c) < 14);
    c = px(img, 47, 47); printf("      centroid (47,47): r%d g%d b%d\n", r5(c), g6(c), b5(c));
    CHECK("centroid is a balanced mix (per-vertex colour latched)", absd(r5(c), b5(c)) <= 3 && r5(c) >= 7 && r5(c) <= 13 && g6(c) >= 14 && g6(c) <= 28);
    CHECK("outside the triangle is untouched", px(img, 100, 100) == 0 && px(img, 3, 3) == 0);
    /* ---- 2. depth test ---- */
    { uint16_t *z = malloc(W * H * 2); uint32_t k; for (k = 0; k < W * H; k++) z[k] = 0xffffu;
      rgb_upload(&b, b.z_off, z, W * H * 2, 0); free(z); }
    rgb_fill(&b, 0, 0, W, H, 0);
    base_state(&st); st.z_cntl = MACH64_Z_EN | MACH64_Z_TEST_LESS | MACH64_Z_MASK_EN;
    m64_cb_init(&cb, cbstore, 8192); m64_emit_state(&cb, &st);
    vtx(&v0, 10, 10, 0x8000, 0xff00ff00u, 0, 0); vtx(&v1, 120, 10, 0x8000, 0xff00ff00u, 0, 0); vtx(&v2, 10, 120, 0x8000, 0xff00ff00u, 0, 0);
    m64_emit_triangle(&cb, &v0, &v1, &v2);                                         /* far green first */
    vtx(&v0, 10, 10, 0x2000, 0xffff0000u, 0, 0); vtx(&v1, 120, 10, 0x2000, 0xffff0000u, 0, 0); vtx(&v2, 10, 120, 0x2000, 0xffff0000u, 0, 0);
    m64_emit_triangle(&cb, &v0, &v1, &v2);                                         /* near red over it */
    vtx(&v0, 10, 10, 0xc000, 0xff0000ffu, 0, 0); vtx(&v1, 120, 10, 0xc000, 0xff0000ffu, 0, 0); vtx(&v2, 10, 120, 0xc000, 0xff0000ffu, 0, 0);
    m64_emit_triangle(&cb, &v0, &v1, &v2);                                         /* farther blue must lose */
    CHECK("flush depth scene", rgb_flush(&b, &cb) == 0 && rgb_fence(&b) == 0); readimg(&b, img);
    c = px(img, 40, 40); printf("      depth pixel (40,40): r%d g%d b%d\n", r5(c), g6(c), b5(c));
    CHECK("nearest (red) wins regardless of order", r5(c) >= 30 && g6(c) < 6 && b5(c) < 3);
    /* ---- 3. textured quad ---- */
    { uint16_t tex[16 * 16], *tb; uint32_t toff, x, y; static const uint16_t q[4] = { 0xf800, 0x07e0, 0x001f, 0xffe0 };
      for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) tex[y * 16 + x] = q[(y >= 8) * 2 + (x >= 8)];
      tb = tex; CHECK("texture alloc", rgb_tex_alloc(&b, 4096, &toff) == 0);
      CHECK("texture upload (swap16 for the little-endian VRAM image)", rgb_upload(&b, toff, tb, 512, 1) == 0);
      rgb_fill(&b, 0, 0, W, H, 0);
      base_state(&st); st.textured = 1; st.tex_scale_3d = 0x0141009bu; st.tex_size_pitch = 4u | (4u << 4) | (4u << 8);
      st.tex_cntl = (1u << 9) | (1u << 23) | (1u << 30); st.tex_0_off = toff; st.tex_fmt = 4; st.tex_maxlog2 = 4;
      m64_cb_init(&cb, cbstore, 8192); m64_emit_state(&cb, &st);
      { float lo = 0.5f / 16.0f, hi = 1.0f - 0.5f / 16.0f;
        vtx(&v0, 16, 16, 0, 0xffffffffu, lo, lo); vtx(&v1, 112, 16, 0, 0xffffffffu, hi, lo);
        vtx(&v2, 16, 112, 0, 0xffffffffu, lo, hi); vtx(&v3, 112, 112, 0, 0xffffffffu, hi, hi);
        m64_emit_triangle(&cb, &v0, &v1, &v2); m64_emit_triangle(&cb, &v1, &v3, &v2); }
      CHECK("flush textured quad", rgb_flush(&b, &cb) == 0 && rgb_fence(&b) == 0); readimg(&b, img);
      c = px(img, 32, 32);  printf("      tex TL: %04x\n", c); CHECK("top-left quadrant red", c == 0xf800);
      c = px(img, 96, 32);  printf("      tex TR: %04x\n", c); CHECK("top-right quadrant green", c == 0x07e0);
      c = px(img, 32, 96);  printf("      tex BL: %04x\n", c); CHECK("bottom-left quadrant blue", c == 0x001f);
      c = px(img, 96, 96);  printf("      tex BR: %04x\n", c); CHECK("bottom-right quadrant yellow", c == 0xffe0);
      rgb_tex_free(&b, toff); }
    /* ---- 4. scissor through the backend + hostile stream ---- */
    rgb_fill(&b, 0, 0, W, H, 0); rgb_scissor(&b, 40, 79, 40, 79);
    base_state(&st); m64_cb_init(&cb, cbstore, 8192); m64_emit_state(&cb, &st);
    vtx(&v0, 0, 0, 0, 0xffffffffu, 0, 0); vtx(&v1, 127, 0, 0, 0xffffffffu, 0, 0); vtx(&v2, 0, 127, 0, 0xffffffffu, 0, 0); m64_emit_triangle(&cb, &v0, &v1, &v2);
    vtx(&v1, 127, 127, 0, 0xffffffffu, 0, 0); m64_emit_triangle(&cb, &v0, &v1, &v2);
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT, 0x1fff0000u);                               /* a stream trying to widen the scissor: routed to the kernel, ignored */
    m64_reg(&cb, MACH64_DST_OFF_PITCH, 0);
    CHECK("flush clipped scene", rgb_flush(&b, &cb) == 0 && rgb_fence(&b) == 0); readimg(&b, img);
    { int in = 0, out = 0, x, y; for (y = 0; y < H; y++) for (x = 0; x < W; x++) { int inside = x >= 40 && x <= 79 && y >= 40 && y <= 79; if (px(img, x, y)) { if (inside) in++; else out++; } }
      printf("      %d pixels inside the scissor, %d outside\n", in, out); CHECK("nothing drawn outside the scissor", out == 0 && in > 1000); }
    /* ---- 5. present ---- */
    rgb_scissor(&b, 0, W - 1, 0, H - 1);
    CHECK("present", rgb_present(&b, 0, 0, W, H, 0, 0) == 0);
    for (i = 0; i < 2; i++) { uint32_t g, w, h, p, bp, bs; (void)g; (void)w; (void)h; (void)p; (void)bp; (void)bs; }
    CHECK("final fence", rgb_fence(&b) == 0);
    printf("      dropped register writes: %u\n", b.dropped);
    rgb_close(&b); free(img);
    printf("gldcore_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
