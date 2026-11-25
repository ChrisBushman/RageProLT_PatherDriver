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
