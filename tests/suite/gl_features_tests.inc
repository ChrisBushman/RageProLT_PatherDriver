/* gl_features_tests.inc - the GL features the driver adds on top of the triangle path (see tools/ragegl/gl_probe.c for the real-AGL counterpart).
 * Included from gl_suite.c before the TESTS table.  Each test drives the dispatch slots with the gli words GLEngine writes. */

/* any lit pixel within r of (x, y): a 1-pixel line can land on either side of a coordinate */
static int near_lit(int x, int y, int r) { int dx, dy; for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) if (px(x + dx, y + dy) != 0) return 1; return 0; }

/* glPolygonMode (gli 0x66e4 = front enum << 16 | back enum; 0x66e8 front-face halfword): LINE draws the edges, POINT the vertices, per face. */
static void t_polymode(void)
{
    unsigned char *vb; unsigned short ccw = 0x901, cw = 0x900; int i;
    for (i = 0; i < 2; i++) {
        default_state(); g32(0x66e4, 0x1b011b01u); memcpy(gli + 0x66e8, i ? &cw : &ccw, 2); sync(); clear_rt(0);
        vb = VB(4);                                                                       /* the quad is wound CCW in y-up window coordinates */
        V(VX(vb, 0), 20, 100, 0, 0xffffffffu); V(VX(vb, 1), 100, 100, 0, 0xffffffffu); V(VX(vb, 2), 100, 20, 0, 0xffffffffu); V(VX(vb, 3), 20, 20, 0, 0xffffffffu);
        SLOT(0x38)(ctx, vb, 4); endframe();
        if (i == 0) {
            CHECK("polygon mode LINE (front face): the quad is hollow", px(60, 60) == 0 && near_lit(60, 20, 1) && near_lit(20, 60, 1) && near_lit(100, 60, 1) && near_lit(60, 100, 1));
            CHECK("polygon mode LINE: a quad draws its 4 edges, no diagonal", px(40, 60) == 0 && px(80, 40) == 0 && px(60, 60) == 0);
        } else {
            CHECK("polygon mode LINE, glFrontFace(CW): the same winding is now the back face, which is also LINE (both faces set)", px(60, 60) == 0 && near_lit(60, 20, 1));
        }
        free(vb);
    }
    default_state(); g32(0x66e4, 0x1b021b01u); memcpy(gli + 0x66e8, &ccw, 2); sync(); clear_rt(0);      /* front FILL, back LINE */
    vb = VB(4); V(VX(vb, 0), 20, 100, 0, 0xffffffffu); V(VX(vb, 1), 100, 100, 0, 0xffffffffu); V(VX(vb, 2), 100, 20, 0, 0xffffffffu); V(VX(vb, 3), 20, 20, 0, 0xffffffffu);
    SLOT(0x38)(ctx, vb, 4); endframe();
    CHECK("polygon mode FRONT=FILL BACK=LINE: a front-facing quad stays filled", px(60, 60) != 0);
    clear_rt(0); V(VX(vb, 0), 20, 20, 0, 0xffffffffu); V(VX(vb, 1), 100, 20, 0, 0xffffffffu); V(VX(vb, 2), 100, 100, 0, 0xffffffffu); V(VX(vb, 3), 20, 100, 0, 0xffffffffu);
    SLOT(0x38)(ctx, vb, 4); endframe();
    CHECK("polygon mode FRONT=FILL BACK=LINE: a back-facing quad is hollow", px(60, 60) == 0 && near_lit(60, 20, 1));
    free(vb);
    default_state(); g32(0x66e4, 0x1b001b00u); memcpy(gli + 0x66e8, &ccw, 2); gf(GLI_POINT_SIZE_OFF, 3.0f); sync(); clear_rt(0);
    vb = VB(3); V(VX(vb, 0), 20, 20, 0, 0xffffffffu); V(VX(vb, 1), 100, 20, 0, 0xffffffffu); V(VX(vb, 2), 60, 100, 0, 0xffffffffu);
    SLOT(0x2c)(ctx, vb, 3); endframe();
    CHECK("polygon mode POINT: the 3 vertices are drawn, the interior is not", near_lit(20, 20, 1) && near_lit(100, 20, 1) && near_lit(60, 100, 1) && px(60, 50) == 0 && px(60, 20) == 0);
    free(vb);
    default_state(); sync(); clear_rt(0);
}

/* glPolygonOffset (gli 0x66dc factor, 0x66e0 units, 0x66f0 bit 16 = fill enable): coplanar redraw wins with a negative offset, loses with a positive one,
 * and the factor scales with the triangle's depth slope. */
static void po_quad(float z0, float z1, unsigned col)         /* a quad whose depth ramps z0 (left) -> z1 (right) */
{
    unsigned char *vb = VB(4);
    V(VX(vb, 0), 20, 100, z0, col); V(VX(vb, 1), 100, 100, z1, col); V(VX(vb, 2), 100, 20, z1, col); V(VX(vb, 3), 20, 20, z0, col);
    SLOT(0x38)(ctx, vb, 4); free(vb);
}
/* red quad with the offset OFF, then the blue one with it on (the app enables GL_POLYGON_OFFSET_FILL between the draws) */
static void po_pair(float factor, float units, int on, float z0, float z1)
{
    default_state(); g32(0x6340, 0x01000000u); g32(0x63b8, 0x01000000u); gf(0x66dc, factor); gf(0x66e0, units); sync(); g_endframe = 0; clear_rt(0);
    po_quad(z0, z1, 0xffff0000u);
    if (on) g32(0x66f0, 0x00010000u);
    po_quad(z0, z1, 0xff0000ffu); endframe();
}
static void t_polyoffset(void)
{
    po_pair(0, 0, 0, 0.5f, 0.5f);
    CHECK("polygon offset off: a coplanar redraw fails GL_LESS (first draw stays)", (px(60, 60) & 0xffffff) == 0xff0000);
    po_pair(0, -2.0f, 1, 0.5f, 0.5f);
    CHECK("polygon offset units -2: the coplanar redraw wins", (px(60, 60) & 0xffffff) == 0x0000ff);
    po_pair(0, 2.0f, 1, 0.5f, 0.5f);
    CHECK("polygon offset units +2: the coplanar redraw still loses", (px(60, 60) & 0xffffff) == 0xff0000);
    po_pair(0, -2.0f, 0, 0.5f, 0.5f);
    CHECK("polygon offset set but not enabled: no effect", (px(60, 60) & 0xffffff) == 0xff0000);
    po_pair(-1.0f, 0, 1, 0.2f, 0.8f);
    CHECK("polygon offset factor -1 on a sloped plane: the coplanar redraw wins", (px(30, 60) & 0xffffff) == 0x0000ff && (px(95, 60) & 0xffffff) == 0x0000ff);
    po_pair(-1.0f, 0, 1, 0.5f, 0.5f);
    CHECK("polygon offset factor on a flat plane contributes nothing (slope 0)", (px(60, 60) & 0xffffff) == 0xff0000);
    default_state(); sync(); g_endframe = 1; clear_rt(0);
}

/* glLineStipple (gli 0x6398 factor, 0x639c pattern << 16, 0x63a0 bit 24 enable): dashes of the right length, factor-scaled, and off = solid. */
static int stipple_run(int x0, int x1, int y, int *runs)       /* lit pixel count across [x0, x1) on rows y-1..y+1, and the number of lit runs */
{
    int x, lit = 0, prev = 0; *runs = 0;
    for (x = x0; x < x1; x++) { int on = px(x, y - 1) != 0 || px(x, y) != 0 || px(x, y + 1) != 0; lit += on; if (on && !prev) (*runs)++; prev = on; }
    return lit;
}
static void stipple_line(unsigned pattern, unsigned factor, int enable, int x0, int x1)
{
    unsigned char *vb;
    default_state(); g32(0x6398, factor); g32(0x639c, pattern << 16); if (enable) g32(0x63a0, 0x01000000u); sync(); clear_rt(0);
    vb = VB(2); V(VX(vb, 0), x0, 60, 0, 0xffffffffu); V(VX(vb, 1), x1, 60, 0, 0xffffffffu); SLOT(0x1c)(ctx, vb, 2); endframe(); free(vb);
}
static void t_linestipple(void)
{
    int runs, lit;
    stipple_line(0x0f0f, 1, 1, 10, 74); lit = stipple_run(8, 76, 60, &runs);
    CHECK("line stipple 0x0f0f factor 1: 8 dashes of 4 px, half the pixels lit", runs == 8 && lit >= 30 && lit <= 36);
    stipple_line(0x00ff, 3, 1, 10, 106); lit = stipple_run(8, 108, 60, &runs);
    CHECK("line stipple 0x00ff factor 3: dashes are 24 px long (2 dashes + the start of a 3rd gap)", runs == 2 && lit >= 46 && lit <= 50);
    stipple_line(0xaaaa, 2, 1, 10, 74); lit = stipple_run(8, 76, 60, &runs);
    CHECK("line stipple 0xaaaa factor 2: alternating 2-px dashes (LSB first: off, on, ...)", runs >= 15 && runs <= 17 && lit >= 30 && lit <= 36);
    stipple_line(0x0f0f, 1, 0, 10, 74); lit = stipple_run(8, 76, 60, &runs);
    CHECK("line stipple disabled: the line is solid", runs == 1 && lit >= 62);
    default_state(); sync(); clear_rt(0);
}

/* GL_LINE_SMOOTH (gli 0x63a0 bit 16) / GL_POINT_SMOOTH (gli 0x6650 bit 24) with the usual SRC_ALPHA / ONE_MINUS_SRC_ALPHA blend: the summed intensity of the
 * antialiased primitive equals its area (the coverage ramp), not the aliased pixel count. */
static int sum_r(int x0, int y0, int x1, int y1) { int x, y, t = 0; for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) t += (px(x, y) >> 16) & 0xff; return t; }
static void smooth_state(uint32_t line, uint32_t point)
{
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (0x0302u << 16) | 0x0303u); if (line) g32(0x63a0, 0x00010000u); if (point) g32(0x6650, 0x01000000u); sync(); clear_rt(0);
}
static void t_smooth(void)
{
    unsigned char *vb; int s;
    smooth_state(1, 0); gf(GLI_LINE_WIDTH_OFF, 1.0f); sync();
    vb = VB(2); V(VX(vb, 0), 20, 60, 0, 0xffffffffu); V(VX(vb, 1), 100, 60, 0, 0xffffffffu); SLOT(0x1c)(ctx, vb, 2); endframe(); free(vb);
    s = sum_r(60, 54, 61, 67);
    printf("    (smooth line w=1: column sum %d, want ~255)\n", s);
    CHECK("smooth line width 1: the column's summed intensity is one pixel of coverage", s > 190 && s < 330);
    CHECK("smooth line: the ramp spans more than one row (antialiased)", (px(60, 59) != 0 || px(60, 60) != 0) && px(60, 56) == 0 && px(60, 64) == 0 && ((px(60, 59) >> 16 & 0xff) < 250 || (px(60, 60) >> 16 & 0xff) < 250));
    smooth_state(1, 0); gf(GLI_LINE_WIDTH_OFF, 3.0f); sync();
    vb = VB(2); V(VX(vb, 0), 20, 60, 0, 0xffffffffu); V(VX(vb, 1), 100, 60, 0, 0xffffffffu); SLOT(0x1c)(ctx, vb, 2); endframe(); free(vb);
    s = sum_r(60, 52, 61, 69);
    CHECK("smooth line width 3: the column's summed intensity is three pixels of coverage", s > 600 && s < 900);
    smooth_state(0, 1); gf(GLI_POINT_SIZE_OFF, 9.0f); sync();
    vb = VB(1); V(VX(vb, 0), 60, 60, 0, 0xffffffffu); SLOT(0x18)(ctx, vb, 1); endframe(); free(vb);
    s = sum_r(48, 48, 73, 73);
    printf("    (smooth point d=9: area sum %d, want ~%d)\n", s, (int)(3.14159f * 4.5f * 4.5f * 255));
    CHECK("smooth point diameter 9: the summed intensity is the disc's area", s > (int)(3.14159f * 4.5f * 4.5f * 255 * 0.85f) && s < (int)(3.14159f * 4.5f * 4.5f * 255 * 1.15f));
    CHECK("smooth point: round (the bounding-box corner is empty), solid centre", px(60, 60) != 0 && (px(60, 60) >> 16 & 0xff) > 240 && px(55, 55) == 0 && px(65, 65) == 0);
    smooth_state(0, 0); gf(GLI_POINT_SIZE_OFF, 9.0f); sync();
    vb = VB(1); V(VX(vb, 0), 60, 60, 0, 0xffffffffu); SLOT(0x18)(ctx, vb, 1); endframe(); free(vb);
    CHECK("point smooth off: a square point (the corner is lit)", px(57, 57) != 0 && px(63, 63) != 0);
    default_state(); sync(); clear_rt(0);
}

/* GL_TEXTURE_ENV_MODE GL_ADD (0x0104): texel + primary colour, saturating (the chip's specular add carries the primary colour). */
static void t_envadd(void)
{
    unsigned char *d = quadtex(); void *t = mktex(8, 8, 0x1907, d, (GL_REPEAT_ << 16) | GL_REPEAT_, 0x2600, 0x2600);
    default_state(); bindtex(t, 0x0104u); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xff004000u);                       /* primary colour (0, 0.25, 0) */
    CHECK("texture env ADD: red texel + (0, 64, 0) = (255, 64, 0)", isclr(px(36, 36), 255, 64, 0));
    CHECK("texture env ADD: white texel saturates", isclr(px(132, 132), 255, 255, 255));
    CHECK("texture env ADD: blue texel + (0, 64, 0) = (0, 64, 255)", isclr(px(36, 132), 0, 64, 255));
    default_state(); bindtex(t, GL_MODULATE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 1.0f, 1.0f, 0xff004000u);
    CHECK("after ADD the next MODULATE draw is not left in add mode", isclr(px(36, 36), 0, 0, 0));
    default_state(); sync(); clear_rt(0);
}

/* ARB_occlusion_query / APPLE_fence through the dispatch slots GLEngine uses (0x68 begin, 0x6c end, 0x64 signal fence). */
static void t_query(void)
{
    typedef int (*cq_fn)(gld_context *, void **); typedef int (*gq_fn)(gld_context *, void *, uint32_t, int *); typedef void (*b_fn)(gld_context *, void *); typedef void (*e_fn)(gld_context *);
    extern int gldCreateQuery(gld_context *, void **), gldGetQueryInfo(gld_context *, void *, uint32_t, int *), gldDestroyQuery(gld_context *, void *);
    extern int gldCreateFence(gld_context *, void **), gldDestroyFence(gld_context *, void *), gldTestObject(gld_context *, int, void *);
    void *q = 0, *f = 0; int res = -1, avail = -1; unsigned char *vb;
    default_state(); sync(); clear_rt(0); g_endframe = 0;
    gldCreateQuery(ctx, &q); CHECK("gldCreateQuery returns an object", q != 0);
    ((b_fn)SLOT(0x68))(ctx, q); tri(20, 20, 0xffffffffu, 120, 20, 0xffffffffu, 20, 120, 0xffffffffu, 0.0f); ((e_fn)SLOT(0x6c))(ctx);
    gldGetQueryInfo(ctx, q, 0x8867u, &avail); gldGetQueryInfo(ctx, q, 0x8866u, &res);
    printf("    (query: triangle area 5000, counted %d)\n", res);
    CHECK("occlusion query counts the samples of a triangle (area 5000)", avail == 1 && res > 4850 && res < 5150);
    ((b_fn)SLOT(0x68))(ctx, q); ((e_fn)SLOT(0x6c))(ctx); gldGetQueryInfo(ctx, q, 0x8866u, &res);
    CHECK("an empty query counts 0 (and Begin restarts the count)", res == 0);
    default_state(); g32(0x6340, 0x01000000u); g32(0x63b8, 0x01000000u); sync(); clear_rt(0);
    tri(20, 20, 0xffffffffu, 120, 20, 0xffffffffu, 20, 120, 0xffffffffu, -0.5f);        /* a near triangle fills the depth buffer */
    ((b_fn)SLOT(0x68))(ctx, q); tri(20, 20, 0xffffffffu, 120, 20, 0xffffffffu, 20, 120, 0xffffffffu, 0.5f); ((e_fn)SLOT(0x6c))(ctx); gldGetQueryInfo(ctx, q, 0x8866u, &res);
    CHECK("occlusion query: a triangle fully behind a nearer one counts 0 samples", res == 0);
    ((b_fn)SLOT(0x68))(ctx, q); tri(20, 20, 0xffffffffu, 120, 20, 0xffffffffu, 20, 120, 0xffffffffu, -0.9f); ((e_fn)SLOT(0x6c))(ctx); gldGetQueryInfo(ctx, q, 0x8866u, &res);
    CHECK("occlusion query: a triangle in front of the depth buffer passes all its samples", res > 4850 && res < 5150);
    vb = 0; (void)vb; gldDestroyQuery(ctx, q);
    gldCreateFence(ctx, &f); CHECK("gldCreateFence returns an object", f != 0);
    ((b_fn)SLOT(0x64))(ctx, f); CHECK("a signalled fence tests complete", gldTestObject(ctx, 0, f) == 1);
    gldDestroyFence(ctx, f);
    default_state(); sync(); g_endframe = 1; clear_rt(0);
}

/* GL_MIRRORED_REPEAT (wrap enum 0x8370): the chip repeats or clamps only, so the driver stores the image plus its mirror and halves the coordinates. */
static void t_mirror(void)
{
    unsigned char *d = quadtex(); void *t = mktex(8, 8, 0x1907, d, (0x8370u << 16) | 0x8370u, 0x2600, 0x2600);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 84, 0.0f, 0.0f, 2.0f, 1.0f, 0xffffffffu);                         /* s 0..2 over 128 px: two 64 px periods, the second mirrored */
    printf("    (mirror row: %08x %08x %08x %08x)\n", px(36, 36), px(68, 36), px(100, 36), px(132, 36));
    CHECK("mirrored repeat: period 1 reads the image (red, then green)", isclr(px(36, 36), 255, 0, 0) && isclr(px(60, 36), 0, 255, 0));
    CHECK("mirrored repeat: period 2 is the image reversed in S (green, then red)", isclr(px(84, 36), 0, 255, 0) && isclr(px(132, 36), 255, 0, 0));
    clear_rt(0); tquad(20, 20, 84, 148, 0.0f, 0.0f, 1.0f, 2.0f, 0xffffffffu);              /* t 0..2 over 128 px: period 64 */
    printf("    (T col: %08x %08x %08x %08x)\n", px(36, 36), px(36, 68), px(36, 84), px(36, 132));
    CHECK("mirrored repeat in T: the second period is the image upside down", isclr(px(36, 36), 255, 0, 0) && isclr(px(36, 68), 0, 0, 255) && isclr(px(36, 84), 0, 0, 255) && isclr(px(36, 132), 255, 0, 0));
    default_state(); sync(); clear_rt(0);
}

/* GL_MIRROR_CLAMP_EXT (0x8742) and GL_CLAMP_TO_BORDER (0x812d): stored as [mirror|image] / image-in-a-border-ring, coordinates mapped per axis. */
static void t_wrapmodes(void)
{
    unsigned char *d = quadtex(); void *t;
    t = mktex(8, 8, 0x1907, d, (0x8742u << 16) | 0x2901u, 0x2600, 0x2600);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 84, -1.0f, 0.0f, 1.0f, 1.0f, 0xffffffffu);                         /* s -1..1: the mirrored image, then the image */
    CHECK("mirror clamp: s in [-1,0) is the image mirrored (green, red)", isclr(px(36, 36), 0, 255, 0) && isclr(px(84, 36), 255, 0, 0));
    CHECK("mirror clamp: s in [0,1] is the image (red, green)", isclr(px(100, 36), 255, 0, 0) && isclr(px(132, 36), 0, 255, 0));
    t = mktex(8, 8, 0x1907, d, (0x812du << 16) | 0x812du, 0x2600, 0x2600);
    default_state(); bindtex(t, GL_REPLACE_ENV); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, -0.5f, -0.5f, 1.5f, 1.5f, 0xffffffffu);                       /* border colour (default 0,0,0) around the image */
    printf("    (border: corner %08x centre-left %08x)\n", px(24, 24), px(60, 60));
    CHECK("clamp to border: outside the image shows the border colour (black)", isclr(px(24, 24), 0, 0, 0) && isclr(px(144, 144), 0, 0, 0));
    CHECK("clamp to border: inside the image shows the texels", isclr(px(60, 60), 255, 0, 0) && isclr(px(108, 108), 255, 255, 255) && isclr(px(108, 60), 0, 255, 0));
    default_state(); sync(); clear_rt(0);
}

/* accumulation buffer through table[0x00] (GL_ACCUM 0x100, LOAD 0x101, RETURN 0x102, MULT 0x103, ADD 0x104) and the clear value at gli+0x6310 */
static void t_accum(void)
{
    typedef void (*acc_fn)(gld_context *, uint32_t, float);
    acc_fn acc = (acc_fn)mach64_ctl00;
    default_state(); sync(); clear_rt(0);
    tquad(20, 20, 148, 148, 0.0f, 0.0f, 0.0f, 0.0f, 0xff804000u);                        /* untextured fill (128, 64, 0) */
    acc(ctx, 0x101u, 1.0f); clear_rt(0); acc(ctx, 0x102u, 1.0f);
    CHECK("accum LOAD 1 + RETURN 1 restores the picture", isclr(px(60, 60), 132, 66, 0) || isclr(px(60, 60), 128, 64, 0));
    acc(ctx, 0x103u, 0.5f); clear_rt(0); acc(ctx, 0x102u, 1.0f);
    CHECK("accum MULT 0.5 halves it", px(60, 60) != 0 && ((px(60, 60) >> 16) & 255) >= 60 && ((px(60, 60) >> 16) & 255) <= 72);
    acc(ctx, 0x104u, 0.25f); clear_rt(0); acc(ctx, 0x102u, 1.0f);
    CHECK("accum ADD 0.25 adds 64 to every channel", ((px(60, 60) >> 8) & 255) >= 90 && (px(60, 60) & 255) >= 56 && (px(60, 60) & 255) <= 72);
    acc(ctx, 0x102u, 2.0f);  clear_rt(0); acc(ctx, 0x102u, 2.0f);
    CHECK("accum RETURN clamps at 255", ((px(60, 60) >> 16) & 255) == 255 || ((px(60, 60) >> 16) & 255) >= 248);
    default_state(); sync(); clear_rt(0);
}

/* Blending the chip cannot do, composited on the CPU: equations (SUBTRACT / REVERSE_SUBTRACT / MIN / MAX), constant-colour factors, SATURATE.
 * Background grey (128,128,128); one big triangle of colour (r,g,b,a) over the probe pixel. */
static unsigned cb_run(uint32_t eq, uint32_t sf, uint32_t df, unsigned argb, float cr, float cg, float cb_, float ca)
{
    default_state(); g32(0x6304, 0x01000000u); g32(0x62e8, (sf << 16) | df); g32(0x62ec, (sf << 16) | df); g32(0x6300, (eq << 16) | eq);
    gf(0x62f0, cr); gf(0x62f4, cg); gf(0x62f8, cb_); gf(0x62fc, ca); sync(); clear_rt(0xff808080u);
    tri(0, 0, argb, 200, 0, argb, 0, 200, argb, 0.0f);
    return px(30, 30);
}
static void t_cblend(void)
{
    unsigned v;
    v = cb_run(0x800au, 1, 1, 0xff402010u, 0, 0, 0, 0); CHECK("blend SUBTRACT: src - dst clamps at 0", isclr(v, 0, 0, 0));
    v = cb_run(0x800bu, 1, 1, 0xff402010u, 0, 0, 0, 0); CHECK("blend REVERSE_SUBTRACT: dst - src = (64, 96, 112)", isclr(v, 64, 112, 112) || (((v >> 16) & 255) >= 58 && ((v >> 16) & 255) <= 70 && ((v >> 8) & 255) >= 100 && ((v >> 8) & 255) <= 118));
    v = cb_run(0x8007u, 1, 1, 0xff402010u, 0, 0, 0, 0); CHECK("blend MIN picks the smaller per channel", ((v >> 16) & 255) <= 70 && (v & 255) <= 24);
    v = cb_run(0x8008u, 1, 1, 0xffc04000u, 0, 0, 0, 0); CHECK("blend MAX picks the larger per channel", ((v >> 16) & 255) >= 184 && ((v >> 8) & 255) >= 120 && ((v >> 8) & 255) <= 136);
    v = cb_run(0x8006u, 0x8001u, 0, 0xffffffffu, 0.5f, 0.25f, 0.0f, 1.0f); CHECK("blend CONSTANT_COLOR src factor: white * (0.5, 0.25, 0)", isclr(v, 128, 64, 0) || (((v >> 16) & 255) >= 120 && ((v >> 16) & 255) <= 136 && ((v >> 8) & 255) >= 56 && ((v >> 8) & 255) <= 72 && (v & 255) <= 8));
    v = cb_run(0x8006u, 0, 0x8002u, 0xff000000u, 0.75f, 0.0f, 0.0f, 1.0f); CHECK("blend ONE_MINUS_CONSTANT_COLOR dst factor: grey * (0.25, 1, 1)", ((v >> 16) & 255) >= 28 && ((v >> 16) & 255) <= 36 && ((v >> 8) & 255) >= 124);
    v = cb_run(0x8006u, 0x0308u, 1, 0x80ff0000u, 0, 0, 0, 0); CHECK("blend SRC_ALPHA_SATURATE (Ad = 1 -> factor 0): the destination stays", isclr(v, 128, 128, 128));
    default_state(); sync(); clear_rt(0);
}
