/*
 * gl_stencil_tests.inc - the stencil test group of gl_suite.c (included there).  The stencil/depth planes are emulated (gld_stencil.c), so every test here compares the
 * planes' TRUTH (mach64_zs_peek) against a plain C model of the GL stencil/depth pipeline run on the same rectangles, and the end-to-end ones also read colour pixels.
 * The model is applied per pixel to the rectangle [x0,x1) x [y0,y1) (what a correct rasteriser covers for integer corners, the two triangles sharing the diagonal).
 * Every test runs both execution modes: hardware Z-as-stencil (simple states) and the CPU path (everything else), and the plane moves between them.
 */
#define S_NEVER 0x200u
#define S_LESS 0x201u
#define S_EQUAL 0x202u
#define S_LEQUAL 0x203u
#define S_GREATER 0x204u
#define S_NOTEQUAL 0x205u
#define S_GEQUAL 0x206u
#define S_ALWAYS 0x207u
static const unsigned ST_OPS[8] = { 0x1e00u, 0x0000u, 0x1e01u, 0x1e02u, 0x1e03u, 0x150au, 0x8507u, 0x8508u };
static const char *const ST_OPN[8] = { "KEEP", "ZERO", "REPLACE", "INCR", "DECR", "INVERT", "INCR_WRAP", "DECR_WRAP" };
static const char *const ST_FN[8] = { "NEVER", "LESS", "EQUAL", "LEQUAL", "GREATER", "NOTEQUAL", "GEQUAL", "ALWAYS" };
#define OPK 0x1e00u
#define OPR 0x1e01u

typedef struct { unsigned func, ref, vm, wm, sf, df, dp; } sface;
typedef struct { int sten, two, depth_en, dfunc, dmask; sface f[2]; } mstate;

static int g_front_ccw = 1;                         /* glFrontFace: 1 = CCW (default) */
static unsigned char ms[W * H]; static unsigned short md[W * H];
static unsigned zi_of(float z) { return z < -0.25f ? 8192u : z > 0.25f ? 24575u : 16384u; }      /* what the chip stores (the 16-bit window depth >> 1) for the clip z values the tests use */

static void st_set(int face, const sface *f)
{
    unsigned fb = 0x6700u + (unsigned)face * 0x10u; unsigned short h;
    g32(fb + 0x0c, f->vm); g32(fb + 0x10, f->ref);
    h = (unsigned short)f->func; memcpy(gli + fb + 0x14, &h, 2); h = (unsigned short)f->sf; memcpy(gli + fb + 0x16, &h, 2);
    h = (unsigned short)f->df; memcpy(gli + fb + 0x18, &h, 2); h = (unsigned short)f->dp; memcpy(gli + fb + 0x1a, &h, 2);
    g32(0x63acu + (unsigned)face * 4u, f->wm);
}
/* push a model state into the stand-in gli and sync */
static void ms_apply(const mstate *m)
{
    st_set(0, &m->f[0]); st_set(1, &m->f[1]);
    g32(0x6734, m->sten ? (1u | (m->two ? 6u : 0u)) : 0u);
    g32(0x6340, m->depth_en ? 0x01000000u : 0u); g32(0x63b8, m->dmask ? 0x01000000u : 0u); g32(0x633c, (unsigned)m->dfunc << 16);
    sync();
}
static int cmpm(unsigned f, unsigned a, unsigned b)
{
    switch (f) { case S_NEVER: return 0; case S_LESS: return a < b; case S_EQUAL: return a == b; case S_LEQUAL: return a <= b; case S_GREATER: return a > b;
                 case S_NOTEQUAL: return a != b; case S_GEQUAL: return a >= b; default: return 1; }
}
static unsigned mop(unsigned op, unsigned s, unsigned ref)
{
    switch (op) { case 0x1e00u: return s; case 0u: return 0; case 0x1e01u: return ref; case 0x1e02u: return s < 255u ? s + 1u : 255u; case 0x1e03u: return s ? s - 1u : 0u;
                  case 0x150au: return ~s & 255u; case 0x8507u: return (s + 1u) & 255u; case 0x8508u: return (s - 1u) & 255u; default: return s; }
}
static void model_clear(unsigned s) { memset(ms, (int)s, sizeof ms); { int i; for (i = 0; i < W * H; i++) md[i] = 0xffffu; } }
static void model_rect(const mstate *m, int face, int x0, int y0, int x1, int y1, unsigned zi)
{
    const sface *f = &m->f[m->two ? face : 0]; int x, y;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
        int i = y * W + x, sp = 1, zp = 1; unsigned s = ms[i], op;
        if (m->sten) sp = cmpm(f->func, f->ref & f->vm, s & f->vm);
        if (m->depth_en && sp) zp = cmpm((unsigned)m->dfunc, zi, md[i]);
        if (m->sten) { op = !sp ? f->sf : (!zp ? f->df : f->dp); ms[i] = (unsigned char)((s & ~f->wm) | (mop(op, s, f->ref) & f->wm)); }
        if (sp && zp && m->depth_en && m->dmask) md[i] = (unsigned short)zi;
    }
}
/* the model against the emulation's truth: returns the number of differing pixels (stencil and depth); prints the first of each */
static int planes_diff(const char *what, int check_depth)
{
    int x, y, bad_s = 0, bad_d = 0;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        unsigned s = mach64_zs_peek(ctx, 0, x, y);
        if (s != ms[y * W + x]) { if (!bad_s) printf("    %s: stencil (%d,%d) got %u want %u\n", what, x, y, s, ms[y * W + x]); bad_s++; }
        if (check_depth) { unsigned d = mach64_zs_peek(ctx, 1, x, y); if (d != md[y * W + x]) { if (!bad_d) printf("    %s: depth (%d,%d) got %u want %u\n", what, x, y, d, md[y * W + x]); bad_d++; } }
    }
    if (bad_s || bad_d) printf("    %s: %d stencil / %d depth pixels differ\n", what, bad_s, bad_d);
    return bad_s + bad_d;
}
static void fl(void) { mach64_flush_batch(ctx); rgb_fence(&P->rb); }
/* a clean frame the test owns: colour 0, stencil 0, depth far, no deferred clears */
static void zs_begin(void)
{
    default_state(); g_endframe = 0; clear_rt(0); P->z_clear_pending = 0; P->frame_new = 0;
    mach64_zs_clear_stencil(ctx, 0); mach64_zs_clear_depth(ctx); model_clear(0);
    g32(0x63b4, 0x01010101u);
}
static void zs_end(void) { g32(0x6734, 0); g32(0x6340, 0); { unsigned short cw = 0; memcpy(gli + 0x66e8, &cw, 2); } g_front_ccw = 1; sync(); fl(); g_endframe = 1; }
/* a rectangle as two triangles: ccw = visually counter-clockwise on screen (front-facing under the default glFrontFace(GL_CCW)) */
static void srect(int x0, int y0, int x1, int y1, float z, unsigned argb, int ccw)
{
    if (ccw) { tri(x0, y0, argb, x0, y1, argb, x1, y1, argb, z); tri(x0, y0, argb, x1, y1, argb, x1, y0, argb, z); }
    else     { tri(x0, y0, argb, x1, y1, argb, x0, y1, argb, z); tri(x0, y0, argb, x1, y0, argb, x1, y1, argb, z); }
}
static sface sf_(unsigned func, unsigned ref, unsigned vm, unsigned wm, unsigned sf, unsigned df, unsigned dp) { sface f; f.func = func; f.ref = ref; f.vm = vm; f.wm = wm; f.sf = sf; f.df = df; f.dp = dp; return f; }
static mstate mstate_simple(sface f) { mstate m; memset(&m, 0, sizeof m); m.sten = 1; m.f[0] = m.f[1] = f; m.dfunc = (int)S_LESS; return m; }
static void rect_m(mstate *m, int x0, int y0, int x1, int y1, float z, unsigned argb, int ccw)
{
    ms_apply(m); srect(x0, y0, x1, y1, z, argb, ccw); model_rect(m, (ccw == g_front_ccw) ? 0 : 1, x0, y0, x1, y1, zi_of(z));
}

/* every stencil op, from clear values that hit the clamp/wrap edges, drawn through the whole pipeline */
static void t_stencil_ops(void)
{
    static const unsigned cv[3] = { 0, 5, 255 }; int i, k;
    for (k = 0; k < 3; k++) for (i = 0; i < 8; i++) {
        mstate m = mstate_simple(sf_(S_ALWAYS, 9, 0xff, 0xff, OPK, OPK, ST_OPS[i])); char nm[96];
        zs_begin(); mach64_zs_clear_stencil(ctx, cv[k]); model_clear(cv[k]);
        rect_m(&m, 40, 40, 100, 90, 0.0f, 0xffffffffu, 1);
        snprintf(nm, sizeof nm, "stencil op %s from %u (rect + outside)", ST_OPN[i], cv[k]);
        CHECK(nm, planes_diff(nm, 0) == 0);
        zs_end();
    }
}

/* the hardware raster rule for an integer rectangle (what the model assumes), through the hardware Z-as-stencil mode */
static void t_stencil_raster_rule(void)
{
    mstate m = mstate_simple(sf_(S_ALWAYS, 1, 0xff, 0xff, OPK, OPK, OPR)); int x, y, n = 0;
    zs_begin(); rect_m(&m, 30, 20, 94, 68, 0.0f, 0xffffffffu, 1);
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) n += (mach64_zs_peek(ctx, 0, x, y) != ms[y * W + x]);
    { unsigned short zz[2]; unsigned o = P->rb.z_off + (unsigned)(40 * P->rb.pitch + 50) * 2u; fl(); rgb_readback2(&P->rb, o & ~3u, zz, 4, 1u);
      printf("    (raw chip Z at (50,40): %04x %04x; plane location mode %u)\n", zz[0], zz[1], P->zs_mode); }
    printf("    (hardware rect coverage differs from the model in %d pixels)\n", n);
    CHECK("stencil via the chip's Z: integer rectangle coverage equals the model", n == 0);
    zs_end();
}

static int lit(int x, int y) { return (px(x, y) & 0x00ffffffu) != 0; }          /* a colour channel set (a masked write can still set the 1555 alpha bit) */
/* stencil bands: value v[k] in the column band x = 20+40k .. 60+40k (full height 20..200), written through the pipeline (REPLACE) */
static void bands(const unsigned v[4])
{
    int k;
    g32(0x63b4, 0);                                                                  /* the bands are stencil-only: colour mask off */
    for (k = 0; k < 4; k++) {
        mstate m = mstate_simple(sf_(S_ALWAYS, v[k], 0xff, 0xff, OPK, OPK, OPR));
        if (v[k]) rect_m(&m, 20 + 40 * k, 20, 60 + 40 * k, 200, 0.0f, 0xff404040u, 1);
    }
    g32(0x63b4, 0x01010101u);
}

/* every compare function x references x value masks against known stencil values: a colour rectangle is drawn only where the test passes */
static void t_stencil_funcs(void)
{
    static const unsigned v[4] = { 0x00, 0x01, 0x12, 0xf3 };
    static const unsigned refs[3] = { 0x01, 0x12, 0x02 }, vms[3] = { 0xff, 0x03, 0xf0 };
    int fi, ri, vi, b; char nm[96];
    for (fi = 0; fi < 8; fi++) {
        int bad = 0, n = 0;
        for (ri = 0; ri < 3; ri++) for (vi = 0; vi < 3; vi++) {
            mstate m = mstate_simple(sf_(S_NEVER + (unsigned)fi, refs[ri], vms[vi], 0xff, OPK, OPK, OPK));
            zs_begin(); bands(v);
            rect_m(&m, 20, 100, 180, 140, 0.0f, 0xff00ff00u, 1); fl();
            for (b = 0; b < 4; b++) {
                int want = cmpm(S_NEVER + (unsigned)fi, refs[ri] & vms[vi], v[b] & vms[vi]), got = lit(40 + 40 * b, 120);
                if (got != want) { if (bad < 6) printf("    func %s ref %02x vmask %02x: band %d (value %02x) drawn=%d want %d\n", ST_FN[fi], refs[ri], vms[vi], b, v[b], got, want); bad++; }
                n++;
            }
            if (planes_diff("funcs planes", 0)) bad++;
            zs_end();
        }
        snprintf(nm, sizeof nm, "stencil func %s: %d band/ref/mask combinations drawn exactly where the test passes", ST_FN[fi], n);
        CHECK(nm, bad == 0);
    }
}

/* value mask / write mask combinations with the ops that read the old value */
static void t_stencil_masks(void)
{
    static const unsigned wms[5] = { 0xff, 0x0f, 0xf0, 0x01, 0x00 }, vms[3] = { 0xff, 0x0f, 0x80 }; static const unsigned ops[4] = { OPR, 0x1e02u, 0x150au, 0x8508u };
    int wi, vi, oi, bad = 0, n = 0;
    for (wi = 0; wi < 5; wi++) for (vi = 0; vi < 3; vi++) for (oi = 0; oi < 4; oi++) {
        mstate m = mstate_simple(sf_(S_LEQUAL, 0x35, vms[vi], wms[wi], ops[oi], ops[(oi + 1) & 3], ops[(oi + 2) & 3]));
        zs_begin(); mach64_zs_clear_stencil(ctx, 0x5a); model_clear(0x5a);
        rect_m(&m, 10, 10, 90, 60, 0.0f, 0xffffffffu, 1); rect_m(&m, 50, 30, 120, 90, 0.0f, 0xffffffffu, 1);
        if (planes_diff("masks", 0)) { bad++; printf("    (write mask %02x value mask %02x ops %s/%s/%s)\n", wms[wi], vms[vi], "sf", "df", "dp"); }
        n++; zs_end();
    }
    { char nm[96]; snprintf(nm, sizeof nm, "stencil value/write masks x ops: %d combinations match the model", n); CHECK(nm, bad == 0); }
}

/* sfail / dpfail / dppass with a live depth test; the depth plane is checked too (written only where both tests pass and the depth mask is on) */
static void t_stencil_depth(void)
{
    int dm, bad = 0;
    for (dm = 0; dm < 2; dm++) {
        mstate m; memset(&m, 0, sizeof m); m.dfunc = (int)S_LESS; m.dmask = 1;
        zs_begin();
        m.depth_en = 1;
        rect_m(&m, 40, 40, 140, 140, 0.0f, 0xffff0000u, 1);                         /* depth floor, stencil off */
        m.sten = 1; m.dmask = dm; m.f[0] = m.f[1] = sf_(S_ALWAYS, 7, 0xff, 0xff, OPK, 0x1e02u, OPR);   /* dpfail INCR, dppass REPLACE 7 */
        rect_m(&m, 80, 80, 180, 180, 0.5f, 0xff00ff00u, 1);                         /* behind the floor in the overlap */
        rect_m(&m, 60, 60, 120, 120, -0.5f, 0xff0000ffu, 1);                        /* in front */
        m.f[0] = m.f[1] = sf_(S_LESS, 3, 0xff, 0xff, 0x1e03u, 0x150au, 0x8507u);    /* real sfail with depth on */
        rect_m(&m, 0, 0, 256, 256, 0.0f, 0xffffffffu, 1);
        if (planes_diff("depth+stencil", 1)) bad++;
        zs_end();
    }
    CHECK("stencil with depth test: sfail/dpfail/dppass and the depth plane match the model (depth mask on and off)", bad == 0);
}

/* z-fail shadow volume in ONE two-sided pass, then the lit pass: the classic use */
static void t_stencil_volume(void)
{
    mstate m; int bad;
    memset(&m, 0, sizeof m); m.dfunc = (int)S_LESS;
    zs_begin(); m.depth_en = 1; m.dmask = 1;
    rect_m(&m, 20, 20, 220, 220, 0.0f, 0xffff0000u, 1);                             /* red floor writes depth */
    g32(0x63b4, 0);                                                                  /* colour mask off for the volume */
    m.sten = 1; m.two = 1; m.dmask = 0;
    m.f[0] = sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, 0x8508u, OPK);                        /* front: DECR_WRAP on depth fail */
    m.f[1] = sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, 0x8507u, OPK);                        /* back : INCR_WRAP on depth fail */
    rect_m(&m, 60, 60, 160, 160, 0.5f, 0xffffffffu, 0);                              /* back faces behind the floor */
    rect_m(&m, 60, 60, 110, 160, 0.5f, 0xffffffffu, 1);                              /* front faces also behind: cancels on the left part */
    rect_m(&m, 0, 0, 256, 256, -0.5f, 0xffffffffu, 1);                               /* front faces in front of the floor: depth passes, no change */
    bad = planes_diff("volume", 1);
    CHECK("z-fail shadow volume: stencil is 1 only where the back face fails and the front face does not", bad == 0 && mach64_zs_peek(ctx, 0, 130, 100) == 1 && mach64_zs_peek(ctx, 0, 80, 100) == 0);
    g32(0x63b4, 0x01010101u);
    m.two = 0; m.f[0] = sf_(S_EQUAL, 0, 0xff, 0xff, OPK, OPK, OPK); m.dmask = 0;
    rect_m(&m, 20, 20, 220, 220, -0.25f, 0xff0000ffu, 1); fl();                      /* lit pass: blue where stencil == 0 and depth passes */
    printf("    (stencil/depth at 40,40: %u / %u; at 80,100: %u / %u)\n", mach64_zs_peek(ctx, 0, 40, 40), mach64_zs_peek(ctx, 1, 40, 40), mach64_zs_peek(ctx, 0, 80, 100), mach64_zs_peek(ctx, 1, 80, 100));
    printf("    (lit pass pixels: %08x %08x %08x; stencil at 130,100 = %u)\n", px(40, 40), px(80, 100), px(130, 100), mach64_zs_peek(ctx, 0, 130, 100));
    CHECK("lit pass: lit areas drawn, the shadow keeps the floor colour", isclr(px(40, 40), 0, 0, 255) && isclr(px(80, 100), 0, 0, 255) && isclr(px(130, 100), 255, 0, 0));
    zs_end();
}

/* front / back selection by winding, glFrontFace, and points/lines (front state) */
static void t_stencil_twoside(void)
{
    int pass, bad = 0;
    for (pass = 0; pass < 2; pass++) {
        mstate m; memset(&m, 0, sizeof m); m.dfunc = (int)S_LESS; m.sten = 1; m.two = 1;
        m.f[0] = sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, OPK, 0x1e02u); m.f[1] = sf_(S_ALWAYS, 9, 0xff, 0xff, OPK, OPK, 0x8508u);
        zs_begin(); mach64_zs_clear_stencil(ctx, 5); model_clear(5);
        if (pass) { unsigned short cw = 0x900; memcpy(gli + 0x66e8, &cw, 2); g_front_ccw = 0; }
        rect_m(&m, 20, 20, 100, 100, 0.0f, 0xffffffffu, 1); rect_m(&m, 60, 60, 140, 140, 0.0f, 0xffffffffu, 0);
        bad += planes_diff(pass ? "two-sided, glFrontFace(CW)" : "two-sided", 0);
        bad += (mach64_zs_peek(ctx, 0, 30, 30) != (pass ? 4u : 6u)) + (mach64_zs_peek(ctx, 0, 130, 130) != (pass ? 6u : 4u));
        zs_end();
    }
    CHECK("two-sided stencil: front state for CCW triangles, back state for CW, and glFrontFace(CW) swaps them", bad == 0);
    { mstate m; unsigned char *vb; memset(&m, 0, sizeof m); m.dfunc = (int)S_LESS; m.sten = 1; m.two = 1;
      m.f[0] = sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, OPK, 0x1e02u); m.f[1] = sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, OPK, 0x1e03u);
      zs_begin(); mach64_zs_clear_stencil(ctx, 5); ms_apply(&m);
      gf(GLI_POINT_SIZE_OFF, 5.0f); sync(); vb = VB(1); V(VX(vb, 0), 128, 128, 0, 0xffffffffu); SLOT(0x18)(ctx, vb, 1); free(vb);
      vb = VB(2); V(VX(vb, 0), 20, 200, 0, 0xffffffffu); V(VX(vb, 1), 200, 200, 0, 0xffffffffu); SLOT(0x1c)(ctx, vb, 2); free(vb);
      printf("    (point centre %u, line %u, untouched %u)\n", mach64_zs_peek(ctx, 0, 128, 128), mach64_zs_peek(ctx, 0, 110, 199), mach64_zs_peek(ctx, 0, 60, 60));
      CHECK("two-sided stencil: points and lines use the front state", mach64_zs_peek(ctx, 0, 128, 128) == 6 && (mach64_zs_peek(ctx, 0, 110, 198) == 6 || mach64_zs_peek(ctx, 0, 110, 199) == 6 || mach64_zs_peek(ctx, 0, 110, 200) == 6 || mach64_zs_peek(ctx, 0, 110, 201) == 6) && mach64_zs_peek(ctx, 0, 60, 60) == 5);
      zs_end(); }
}

/* a rasteriser that drops or doubles a pixel on a shared edge would corrupt every INCR-based technique: nothing may be hit twice or missed */
static void Vf(unsigned char *v, float xf, float y, float z, unsigned argb)
{
    unsigned sh_ = *(const uint32_t *)((const unsigned char *)ctx + 0x1f4); float yf = (float)sh_ - y;
    V(v, 0, 0, z, argb); memcpy(v + 0x00, &xf, 4); memcpy(v + 0x04, &yf, 4);
}
static void t_stencil_watertight(void)
{
    mstate m = mstate_simple(sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, OPK, 0x1e02u)); int x, y, twice = 0, miss = 0, inside = 0, i; const float cx = 128.3f, cy = 128.7f, R = 60.0f;
    static const float ca[7] = { 1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f, 1.0f }, sa[7] = { 0.0f, 0.8660254f, 0.8660254f, 0.0f, -0.8660254f, -0.8660254f, 0.0f };
    zs_begin(); ms_apply(&m);
    for (i = 0; i < 6; i++) {
        unsigned char a[MACH64_GLIVERTEX_STRIDE], b[MACH64_GLIVERTEX_STRIDE], c[MACH64_GLIVERTEX_STRIDE];
        Vf(a, cx, cy, 0.0f, 0xffffffffu); Vf(b, cx + R * ca[i], cy + R * sa[i], 0.0f, 0xffffffffu); Vf(c, cx + R * ca[i + 1], cy + R * sa[i + 1], 0.0f, 0xffffffffu);
        draw_tri(a, b, c, 3);
    }
    srect(10, 10, 74, 58, 0.0f, 0xffffffffu, 1);
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        unsigned s = mach64_zs_peek(ctx, 0, x, y); float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
        if (s > 1) twice++;
        if (dx * dx + dy * dy < 50.0f * 50.0f) { inside++; if (s != 1) miss++; }
        if (x >= 10 && x < 74 && y >= 10 && y < 58 && s != 1) miss++;
    }
    printf("    (%d pixels inside the guaranteed disc, %d hit twice, %d missed)\n", inside, twice, miss);
    CHECK("stencil INCR over a 6-triangle fan with fractional vertices and a quad: no pixel hit twice, none missed", twice == 0 && miss == 0 && inside > 7000);
    zs_end();
}

/* a random state machine: hundreds of draws through every mode, the plane moving between the chip and the CPU, checked against the model */
static unsigned g_lcg;
static unsigned rnd(void) { g_lcg = g_lcg * 1664525u + 1013904223u; return g_lcg >> 8; }
static sface rface(int friendly)
{
    static const unsigned refs[8] = { 0, 1, 2, 3, 7, 0x55, 0xff, 0x80 }, vms[4] = { 0xff, 0xff, 0x0f, 0xf0 }, wms[6] = { 0xff, 0xff, 0x0f, 0xf0, 0x01, 0x00 };
    sface f; unsigned o = rnd();
    f.func = S_NEVER + (rnd() & 7u); f.ref = refs[rnd() & 7u]; f.vm = vms[rnd() & 3u]; f.wm = wms[rnd() % 6u];
    f.sf = ST_OPS[rnd() & 7u]; f.df = ST_OPS[rnd() & 7u]; f.dp = ST_OPS[rnd() & 7u];
    if (friendly) {
        f.vm = f.wm = 0xff; if (f.func != S_ALWAYS) f.sf = OPK;
        f.dp = (o & 1u) ? OPK : OPR; if (f.dp == OPR && o % 7u == 0) f.dp = (f.func == S_ALWAYS || f.ref == 0) ? 0u : OPK;
    }
    return f;
}
static void t_stencil_random(void)
{
    static const float zs[3] = { -0.5f, 0.0f, 0.5f }; int seed, step, bad = 0, steps = 0;
    for (seed = 1; seed <= 3; seed++) {
        g_lcg = (unsigned)seed * 2654435761u; zs_begin();
        for (step = 0; step < 120; step++) {
            mstate m; int x0 = (int)(rnd() % 200u), y0 = (int)(rnd() % 200u), x1 = x0 + 1 + (int)(rnd() % 56u), y1 = y0 + 1 + (int)(rnd() % 56u), friendly = (rnd() % 10u) < 4u, ccw = (int)(rnd() & 1u);
            memset(&m, 0, sizeof m);
            m.sten = (rnd() % 6u) != 0; m.two = !friendly && (rnd() % 6u) == 0; m.depth_en = !friendly && (rnd() % 3u) == 0; m.dfunc = (int)(S_NEVER + (rnd() & 7u)); m.dmask = (int)(rnd() & 1u);
            m.f[0] = rface(friendly); m.f[1] = m.two ? rface(0) : m.f[0];
            g32(0x63b4, (rnd() % 8u) == 0 ? 0u : 0x01010101u);
            rect_m(&m, x0, y0, x1, y1, zs[rnd() % 3u], 0xff808080u, ccw); steps++;
            if (step % 20 == 19 || step == 119) { char nm[48]; snprintf(nm, sizeof nm, "random seed %d step %d", seed, step); if (planes_diff(nm, 1)) { bad++; break; } }
        }
        zs_end();
    }
    { char nm[96]; snprintf(nm, sizeof nm, "random stencil/depth state machine: %d draws match the model (stencil and depth planes)", steps); CHECK(nm, bad == 0); }
}

/* the depth plane survives excursions into every stencil mode and back, and the colour results follow it */
static void t_stencil_persist(void)
{
    mstate m; int bad;
    memset(&m, 0, sizeof m); m.dfunc = (int)S_LESS; zs_begin(); m.depth_en = 1; m.dmask = 1;
    rect_m(&m, 40, 40, 140, 140, 0.0f, 0xffff0000u, 1);                                              /* hardware depth: the floor */
    m.sten = 1; m.f[0] = m.f[1] = sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, OPK, 0x1e02u);                    /* CPU stencil with the depth test on */
    rect_m(&m, 80, 80, 180, 180, 0.5f, 0xff00ff00u, 1);
    m.sten = 0; rect_m(&m, 60, 60, 120, 120, 0.5f, 0xff0000ffu, 1);                                   /* back to hardware depth: the floor must still reject this */
    m.depth_en = 0; m.sten = 1; m.f[0] = m.f[1] = sf_(S_ALWAYS, 3, 0xff, 0xff, OPK, OPK, OPR);         /* chip-Z stencil: the depth plane moves out */
    rect_m(&m, 10, 10, 60, 60, 0.0f, 0xffffffffu, 1);
    m.sten = 0; m.depth_en = 1; rect_m(&m, 0, 0, 256, 256, 0.0f, 0xff00ffffu, 1); fl();               /* depth back in the chip */
    printf("    (persist pixels: %08x %08x %08x)\n", px(70, 70), px(100, 100), px(150, 150));
    CHECK("depth plane survives CPU-stencil and chip-stencil excursions: floor keeps rejecting, open areas draw", isclr(px(70, 70), 255, 0, 0) && isclr(px(100, 100), 255, 0, 0) && isclr(px(150, 150), 0, 255, 255));
    bad = planes_diff("persist", 1);
    CHECK("depth and stencil planes match the model after the excursions", bad == 0);
    zs_end();
}

/* glClear owes the stencil plane the clear value, wherever the plane currently lives */
static void t_stencil_clear(void)
{
    mstate m = mstate_simple(sf_(S_ALWAYS, 9, 0xff, 0xff, OPK, OPK, OPR)); int bad = 0, i;
    for (i = 0; i < 3; i++) {                                             /* plane in the chip (simple state), on the CPU (INCR), or untouched (uniform) */
        zs_begin(); mach64_zs_clear_stencil(ctx, 40u + (unsigned)i); model_clear(40u + (unsigned)i);
        if (i == 0) rect_m(&m, 30, 30, 70, 70, 0.0f, 0xffffffffu, 1);
        if (i == 1) { mstate k = m; k.f[0].dp = k.f[1].dp = 0x1e02u; rect_m(&k, 30, 30, 70, 70, 0.0f, 0xffffffffu, 1); }
        mach64_zs_clear_stencil(ctx, 77); model_clear(77);
        bad += planes_diff("clear", 1);
        zs_end();
    }
    CHECK("stencil clear value reaches the plane from the chip, the CPU copy and the uniform state", bad == 0);
    zs_begin(); g32(0x6334, 33); g32(0x308, 0x10); P->frame_new = 1;                                    /* a new frame whose glClear is flagged */
    m.f[0].dp = m.f[1].dp = OPK; rect_m(&m, 5, 5, 6, 6, 0.0f, 0xffffffffu, 1); fl();
    CHECK("a flagged glClear at frame start clears the stencil plane to glClearStencil", mach64_zs_peek(ctx, 0, 100, 100) == 33);
    g32(0x6334, 0); zs_end();
}

/* the CPU path hands the hardware scissor rectangles: a 1-pixel checker (more than 512 rectangles), and colour-masked draws */
static void t_stencil_scissor(void)
{
    mstate w = mstate_simple(sf_(S_ALWAYS, 1, 0xff, 0xff, OPK, OPK, OPR)), t; int x, y, bad = 0, n = 0;
    zs_begin();
    for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) if ((x + y) & 1) rect_m(&w, 20 + x, 20 + y, 21 + x, 21 + y, 0.0f, 0xffffffffu, 1);
    t = mstate_simple(sf_(S_EQUAL, 1, 0x7f, 0xff, OPK, OPK, OPK));                                        /* a value mask: the CPU path */
    rect_m(&t, 15, 15, 65, 65, 0.0f, 0xff00ff00u, 1); fl();
    for (y = 0; y < 40; y++) for (x = 0; x < 40; x++) { int want = ((x + y) & 1) != 0; if (lit(20 + x, 20 + y) != want) bad++; n++; }
    printf("    (checker: %d of %d cells wrong)\n", bad, n);
    CHECK("scissor rectangles: a 1-pixel checker (800 rectangles, past the batch limit) is drawn exactly on the lit cells", bad == 0);
    zs_end();
    zs_begin(); g32(0x63b4, 0); w = mstate_simple(sf_(S_ALWAYS, 5, 0xff, 0xff, OPK, OPK, 0x1e02u));
    rect_m(&w, 50, 50, 100, 100, 0.0f, 0xffffffffu, 1); fl();
    CHECK("colour mask off: the stencil still updates and nothing is drawn", planes_diff("colourmask", 0) == 0 && !lit(70, 70));
    zs_end();
}

/* the CPU rasteriser must cover exactly the pixels the chip covers, edge ties included (the CPU decides WHICH pixels the chip may draw): compare both on random triangles */
static void tri_f(float x0, float y0, float x1, float y1, float x2, float y2)
{
    unsigned char a[MACH64_GLIVERTEX_STRIDE], b[MACH64_GLIVERTEX_STRIDE], c[MACH64_GLIVERTEX_STRIDE];
    Vf(a, x0, y0, 0.0f, 0xffffffffu); Vf(b, x1, y1, 0.0f, 0xffffffffu); Vf(c, x2, y2, 0.0f, 0xffffffffu); draw_tri(a, b, c, 3);
}
static int hw_covered(int x, int y) { return lit(x, y); }
static void t_stencil_agree(void)
{
    int n, bad = 0, ties_bad = 0, total = 0, tiecase;
    g_lcg = 12345u;
    for (n = 0; n < 60; n++) {
        float v[6]; int i, x, y, diffs = 0, minx = 255, maxx = 0, miny = 255, maxy = 0; mstate m = mstate_simple(sf_(S_ALWAYS, 0, 0xff, 0xff, OPK, OPK, 0x1e02u));
        tiecase = n < 30;                                                    /* the first half: integer corners (45-degree edges through pixel centres) */
        for (i = 0; i < 6; i++) v[i] = tiecase ? (float)(20 + (int)(rnd() % 40u)) : (float)(20 * 4 + (int)(rnd() % 160u)) * 0.25f;
        for (i = 0; i < 3; i++) { int xx = (int)v[2 * i], yy = (int)v[2 * i + 1]; if (xx < minx) minx = xx; if (xx > maxx) maxx = xx; if (yy < miny) miny = yy; if (yy > maxy) maxy = yy; }
        zs_begin(); sync(); tri_f(v[0], v[1], v[2], v[3], v[4], v[5]); fl();                  /* chip, no stencil */
        { static unsigned char hw[H][W]; for (y = miny; y <= maxy + 1; y++) for (x = minx - 1; x <= maxx + 1; x++) hw[y][x] = (unsigned char)hw_covered(x, y);
          zs_end(); zs_begin(); g32(0x63b4, 0); ms_apply(&m); tri_f(v[0], v[1], v[2], v[3], v[4], v[5]);              /* CPU stencil, colour masked */
          for (y = miny; y <= maxy + 1; y++) for (x = minx - 1; x <= maxx + 1; x++) {
              int cpu = mach64_zs_peek(ctx, 0, x, y) != 0;
              if (cpu != hw[y][x]) { if (diffs < 3) printf("    tri %d (%.2f,%.2f %.2f,%.2f %.2f,%.2f): pixel (%d,%d) chip=%d cpu=%d\n", n, v[0], v[1], v[2], v[3], v[4], v[5], x, y, hw[y][x], cpu); diffs++; }
              total++; } }
        if (diffs) { bad++; if (tiecase) ties_bad++; }
        zs_end();
    }
    printf("    (%d of 60 triangles differ, %d of them integer-corner; %d pixels compared)\n", bad, ties_bad, total);
    CHECK("CPU rasteriser covers exactly the chip's pixels on 60 random triangles (integer corners and quarter pixels)", bad == 0);
}
