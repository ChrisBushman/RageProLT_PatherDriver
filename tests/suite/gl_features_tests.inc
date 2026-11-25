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
