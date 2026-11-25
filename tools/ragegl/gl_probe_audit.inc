/* Feature-audit probes (docs/gl-feature-audit.md): OpenGL 1.1 core features and the extensions GLEngine can advertise.  Expectations come from the spec and
 * were validated against the GeForce reference (AGL_MAIN=1) before being used against the Rage driver.  Included by gl_probe.c. */
#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_POST_COLOR_MATRIX_RED_SCALE
#define GL_POST_COLOR_MATRIX_RED_SCALE 0x80B4
#define GL_POST_COLOR_MATRIX_GREEN_SCALE 0x80B5
#define GL_POST_COLOR_MATRIX_BLUE_SCALE 0x80B6
#define GL_POST_COLOR_MATRIX_GREEN_BIAS 0x80B9
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_ABGR_EXT
#define GL_ABGR_EXT 0x8000
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_ARRAY_BUFFER_ARB
#define GL_ARRAY_BUFFER_ARB 0x8892
#define GL_STATIC_DRAW_ARB 0x88E4
#endif
#ifndef GL_COMPRESSED_RGB_ARB
#define GL_COMPRESSED_RGB_ARB 0x84ED
#endif
#ifndef GL_DEPTH_TEXTURE_MODE_ARB
#define GL_DEPTH_TEXTURE_MODE_ARB 0x884B
#endif

static int lit(int x, int y) { int r, g, b; px(x, y, &r, &g, &b); return r > 128 || g > 128 || b > 128; }
static void aq(const char *name, int ok, const char *note) { report(name, ok < 0 ? -1 : ok, note); }

/* ---- rasterisation ------------------------------------------------------ */
static void a_wide_point(void)
{
    int a, b, c, d; reset(0, 0, 0); glPointSize(5); glColor3f(1, 1, 1); glBegin(GL_POINTS); glVertex2f(64.5f, 64.5f); glEnd();
    a = lit(64, 64) && lit(62, 62) && lit(66, 66); b = !lit(60, 64) && !lit(68, 64) && !lit(64, 60) && !lit(64, 68); c = 1; d = 1; (void)c; (void)d;
    aq("wide point (size 5, aliased)", glerr() ? -1 : (a && b), a ? "" : "centre square not filled");
}
static void a_wide_line(void)
{
    reset(0, 0, 0); glLineWidth(3); glColor3f(1, 1, 1); glBegin(GL_LINES); glVertex2f(10, 64.5f); glVertex2f(100, 64.5f); glEnd();
    aq("wide line (width 3, aliased)", glerr() ? -1 : (lit(50, 64) && lit(50, 63) && lit(50, 65) && !lit(50, 60) && !lit(50, 68)), 0);
}
static void a_polygon_stipple(void)
{
    GLubyte pat[128]; int i, ok; for (i = 0; i < 128; i++) pat[i] = (i / 4) & 1 ? 0xff : 0x00;      /* odd rows set */
    reset(0, 0, 0); glPolygonStipple(pat); glEnable(GL_POLYGON_STIPPLE); quad(10, 10, 100, 100, 1, 1, 1, 1); glDisable(GL_POLYGON_STIPPLE);
    ok = (lit(50, 49) != lit(50, 50)) && (lit(50, 51) == lit(50, 49)) && (lit(70, 49) == lit(50, 49));
    aq("polygon stipple", glerr() ? -1 : ok, 0);
}
static void a_polygon_smooth(void)
{
    reset(0, 0, 0); glEnable(GL_POLYGON_SMOOTH); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); quad(10, 10, 100, 100, 1, 1, 1, 1); glDisable(GL_POLYGON_SMOOTH);
    aq("polygon smooth (interior drawn)", glerr() ? -1 : near3(50, 50, 255, 255, 255, 40), 0);
}
static void a_polyoffset_line(void)
{
    int ok;
    reset(0, 0, 0); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClear(GL_DEPTH_BUFFER_BIT);
    quad(10, 10, 100, 100, 1, 0, 0, 1);                                              /* fill at z = 0 */
    glEnable(GL_POLYGON_OFFSET_LINE); glPolygonOffset(-1, -4); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glDisable(GL_CULL_FACE);
    glColor3f(0, 1, 0); glBegin(GL_QUADS); glVertex3f(30.5f, 30.5f, 0); glVertex3f(80.5f, 30.5f, 0); glVertex3f(80.5f, 80.5f, 0); glVertex3f(30.5f, 80.5f, 0); glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_POLYGON_OFFSET_LINE);
    ok = near3(55, 30, 0, 255, 0, 30);                                              /* the outline's bottom edge wins over the coplanar fill */
    aq("polygon offset LINE (outline over coplanar fill)", glerr() ? -1 : ok, 0);
}
static void a_polyoffset_point(void)
{
    int ok;
    reset(0, 0, 0); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClear(GL_DEPTH_BUFFER_BIT);
    quad(10, 10, 100, 100, 1, 0, 0, 1);
    glEnable(GL_POLYGON_OFFSET_POINT); glPolygonOffset(-1, -4); glPolygonMode(GL_FRONT_AND_BACK, GL_POINT); glPointSize(3);
    glColor3f(0, 1, 0); glBegin(GL_QUADS); glVertex3f(30, 30, 0); glVertex3f(80, 30, 0); glVertex3f(80, 80, 0); glVertex3f(30, 80, 0); glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_POLYGON_OFFSET_POINT);
    ok = near3(30, 30, 0, 255, 0, 30);
    aq("polygon offset POINT", glerr() ? -1 : ok, 0);
}

/* ---- texturing ---------------------------------------------------------- */
static void a_tex1d(void)
{
    static const unsigned char d[6] = { 255, 0, 0, 0, 255, 0 }; int ok;
    reset(0, 0, 0); glEnable(GL_TEXTURE_1D);
    { GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_1D, id); }
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, d);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord1f(0); glVertex2f(10, 10); glTexCoord1f(1); glVertex2f(100, 10); glTexCoord1f(1); glVertex2f(100, 100); glTexCoord1f(0); glVertex2f(10, 100); glEnd();
    ok = near3(30, 50, 255, 0, 0, 20) && near3(80, 50, 0, 255, 0, 20); glDisable(GL_TEXTURE_1D);
    aq("1D texture", glerr() ? -1 : ok, 0);
}
static void a_tex_border(void)
{
    unsigned char d[4 * 3 * 3]; int x, y, ok; GLuint id;     /* 2x1 interior + 1 texel border on every side: 4x3 texels */
    for (y = 0; y < 3; y++) for (x = 0; x < 4; x++) { unsigned char *p = d + (y * 4 + x) * 3; int inside = x >= 1 && x <= 2 && y == 1; p[0] = inside ? (x == 1 ? 255 : 0) : 0; p[1] = inside ? (x == 1 ? 0 : 255) : 0; p[2] = inside ? 0 : 255; }
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 3, getenv("BORD") ? atoi(getenv("BORD")) : 1, GL_RGB, GL_UNSIGNED_BYTE, d);               /* the size includes the border: 2 + 2 by 1 + 2 */
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(-0.25f, 0.2f); glVertex2f(10, 10); glTexCoord2f(1.25f, 0.2f); glVertex2f(100, 10); glTexCoord2f(1.25f, 0.8f); glVertex2f(100, 100); glTexCoord2f(-0.25f, 0.8f); glVertex2f(10, 100); glEnd();
    ok = near3(15, 50, 0, 0, 255, 20) && near3(40, 50, 255, 0, 0, 20) && near3(70, 50, 0, 255, 0, 20) && near3(95, 50, 0, 0, 255, 20); glDisable(GL_TEXTURE_2D);
    if (getenv("ROW")) { int xx, r, g, b; char nb[16]; for (xx = 12; xx < 100; xx += 6) { px(xx, 50, &r, &g, &b); snprintf(nb, sizeof nb, " %d:%c", xx, r > 128 ? 'R' : g > 128 ? 'G' : b > 128 ? 'B' : '.'); strncat(g_act, nb, sizeof g_act - strlen(g_act) - 1); } }
    aq("texture border (1-texel border, CLAMP)", glerr() ? -1 : ok, 0);
}
static void a_tex_border_rep(void)
{
    unsigned char d[4 * 3 * 3]; int x, y, ok; GLuint id;     /* 2x1 interior + 1 texel border on every side: 4x3 texels */
    for (y = 0; y < 3; y++) for (x = 0; x < 4; x++) { unsigned char *p = d + (y * 4 + x) * 3; int inside = x >= 1 && x <= 2 && y == 1; p[0] = inside ? (x == 1 ? 255 : 0) : 0; p[1] = inside ? (x == 1 ? 0 : 255) : 0; p[2] = inside ? 0 : 255; }
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 3, getenv("BORD") ? atoi(getenv("BORD")) : 1, GL_RGB, GL_UNSIGNED_BYTE, d);               /* the size includes the border: 2 + 2 by 1 + 2 */
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(-0.25f, 0.2f); glVertex2f(10, 10); glTexCoord2f(1.25f, 0.2f); glVertex2f(100, 10); glTexCoord2f(1.25f, 0.8f); glVertex2f(100, 100); glTexCoord2f(-0.25f, 0.8f); glVertex2f(10, 100); glEnd();
    ok = near3(15, 50, 0, 255, 0, 20) && near3(40, 50, 255, 0, 0, 20) && near3(70, 50, 0, 255, 0, 20) && near3(95, 50, 255, 0, 0, 20); glDisable(GL_TEXTURE_2D);
    if (getenv("ROW")) { int xx, r, g, b; char nb[16]; for (xx = 12; xx < 100; xx += 6) { px(xx, 50, &r, &g, &b); snprintf(nb, sizeof nb, " %d:%c", xx, r > 128 ? 'R' : g > 128 ? 'G' : b > 128 ? 'B' : '.'); strncat(g_act, nb, sizeof g_act - strlen(g_act) - 1); } }
    aq("texture border (1-texel border, REPEAT: interior only)", glerr() ? -1 : ok, 0);
}
static void a_texgen(void)
{
    GLfloat pl[4] = { 1.0f / 90.0f, 0, 0, -10.0f / 90.0f }; GLuint id; int ok;
    reset(0, 0, 0); id = mktex(GL_TEXTURE_2D, 2, 1, RG2, GL_NEAREST, GL_CLAMP); (void)id; glEnable(GL_TEXTURE_2D);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR); glTexGenfv(GL_S, GL_OBJECT_PLANE, pl); glEnable(GL_TEXTURE_GEN_S); glTexCoord2f(0, 0.5f);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glDisable(GL_TEXTURE_GEN_S); ok = near3(30, 50, 255, 0, 0, 20) && near3(80, 50, 0, 255, 0, 20); glDisable(GL_TEXTURE_2D);
    aq("texgen OBJECT_LINEAR", glerr() ? -1 : ok, 0);
}
static void a_texgen_sphere(void)
{
    GLuint id; int ok; reset(0, 0, 0); id = mktex(GL_TEXTURE_2D, 2, 1, RG2, GL_NEAREST, GL_CLAMP); (void)id; glEnable(GL_TEXTURE_2D);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP); glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP); glEnable(GL_TEXTURE_GEN_S); glEnable(GL_TEXTURE_GEN_T);
    glColor3f(1, 1, 1); glNormal3f(0, 0, 1); glBegin(GL_QUADS); glVertex2f(10, 10); glVertex2f(100, 10); glVertex2f(100, 100); glVertex2f(10, 100); glEnd();
    glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T); ok = lit(50, 50); glDisable(GL_TEXTURE_2D);      /* normal toward the eye maps to the sphere-map centre: some texel, drawn */
    aq("texgen SPHERE_MAP (drawn)", glerr() ? -1 : ok, 0);
}
static void a_texmatrix(void)
{
    GLuint id; int ok; reset(0, 0, 0); id = mktex(GL_TEXTURE_2D, 2, 1, RG2, GL_NEAREST, GL_REPEAT); (void)id; glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glTranslatef(0.5f, 0, 0); glMatrixMode(GL_MODELVIEW);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(0, 0.5f); glVertex2f(10, 10); glTexCoord2f(1, 0.5f); glVertex2f(100, 10); glTexCoord2f(1, 0.5f); glVertex2f(100, 100); glTexCoord2f(0, 0.5f); glVertex2f(10, 100); glEnd();
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
    ok = near3(30, 50, 0, 255, 0, 20) && near3(80, 50, 255, 0, 0, 20); glDisable(GL_TEXTURE_2D);
    aq("texture matrix (translate by 0.5)", glerr() ? -1 : ok, 0);
}
static void a_texsubimage(void)
{
    static const unsigned char red[12] = { 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0 }, grn[6] = { 0, 255, 0, 0, 255, 0 }; GLuint id; int ok;
    reset(0, 0, 0); id = mktex(GL_TEXTURE_2D, 4, 1, red, GL_NEAREST, GL_CLAMP); (void)id; glEnable(GL_TEXTURE_2D);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 2, 1, GL_RGB, GL_UNSIGNED_BYTE, grn);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord2f(0, 0.5f); glVertex2f(10, 10); glTexCoord2f(1, 0.5f); glVertex2f(100, 10); glTexCoord2f(1, 0.5f); glVertex2f(100, 100); glTexCoord2f(0, 0.5f); glVertex2f(10, 100); glEnd();
    ok = near3(25, 50, 255, 0, 0, 20) && near3(85, 50, 0, 255, 0, 20); glDisable(GL_TEXTURE_2D);
    aq("glTexSubImage2D (right half replaced)", glerr() ? -1 : ok, 0);
}
static void a_texsubimage1d(void)
{
    static const unsigned char red[6] = { 255, 0, 0, 255, 0, 0 }, grn[3] = { 0, 255, 0 }; int ok;
    reset(0, 0, 0); glEnable(GL_TEXTURE_1D); { GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_1D, id); }
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, red); glTexSubImage1D(GL_TEXTURE_1D, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, grn);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glTexCoord1f(0); glVertex2f(10, 10); glTexCoord1f(1); glVertex2f(100, 10); glTexCoord1f(1); glVertex2f(100, 100); glTexCoord1f(0); glVertex2f(10, 100); glEnd();
    ok = near3(30, 50, 255, 0, 0, 20) && near3(80, 50, 0, 255, 0, 20); glDisable(GL_TEXTURE_1D);
    aq("glTexSubImage1D", glerr() ? -1 : ok, 0);
}
static void a_tex_objects(void)
{
    static const unsigned char blue[3] = { 0, 0, 255 }, red[3] = { 255, 0, 0 }; GLuint a, b; int ok = 1; GLboolean res[2]; GLuint ids[2];
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); a = mktex(GL_TEXTURE_2D, 1, 1, red, GL_NEAREST, GL_CLAMP); b = mktex(GL_TEXTURE_2D, 1, 1, blue, GL_NEAREST, GL_CLAMP);
    glBindTexture(GL_TEXTURE_2D, a); tquad(0, 1, 10, 50); glBindTexture(GL_TEXTURE_2D, b); tquad(0, 1, 60, 100);
    ok = near3(30, 50, 255, 0, 0, 20) && near3(80, 50, 0, 0, 255, 20);
    ids[0] = a; ids[1] = b; { GLfloat pr[2] = { 1, 0.5f }; glPrioritizeTextures(2, ids, pr); } (void)glAreTexturesResident(2, ids, res);
    glDeleteTextures(1, &a); { GLuint c = 0; glGenTextures(1, &c); glBindTexture(GL_TEXTURE_2D, c); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, red); }
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); tquad(0, 1, 10, 50); ok = ok && near3(30, 50, 255, 0, 0, 20); glDisable(GL_TEXTURE_2D);
    aq("texture objects (bind, prioritise, delete, reuse)", glerr() ? -1 : ok, 0);
}
static void a_tex_internal(void)
{
    static const unsigned char d[3] = { 255, 128, 0 }; int ok; GLuint id;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_ARB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, d);
    tquad(0, 1, 10, 100); ok = near3(50, 50, 255, 128, 0, 40); glDisable(GL_TEXTURE_2D);
    aq("generic compressed internal format", glerr() ? -1 : ok, 0);
}
static void a_tex_bgra(void)
{
    static const unsigned char bgra[4] = { 0, 0, 255, 255 }; int ok; GLuint id; unsigned char out[4] = { 0, 0, 0, 0 };   /* B,G,R,A of red */
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra); (void)out;
    tquad(0, 1, 10, 100); ok = near3(50, 50, 255, 0, 0, 20); glDisable(GL_TEXTURE_2D);
    aq("EXT_bgra texture upload", glerr() ? -1 : ok, 0);
}
static void a_tex_abgr(void)
{
    static const unsigned char abgr[4] = { 255, 0, 255, 0 }; int ok; GLuint id;         /* A,B,G,R of red-with-blue-zero: a=255 b=0 g=255 r=0 -> green */
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_ABGR_EXT, GL_UNSIGNED_BYTE, abgr);
    tquad(0, 1, 10, 100); ok = near3(50, 50, 0, 255, 0, 20); glDisable(GL_TEXTURE_2D);
    aq("EXT_abgr texture upload", glerr() ? -1 : ok, 0);
}
static void a_tex_maxlevel(void)
{
    unsigned char l0[4 * 4 * 3], l1[2 * 2 * 3], l2[3]; int i, ok; GLuint id;
    for (i = 0; i < 16; i++) { l0[i * 3] = 255; l0[i * 3 + 1] = 0; l0[i * 3 + 2] = 0; } for (i = 0; i < 4; i++) { l1[i * 3] = 0; l1[i * 3 + 1] = 255; l1[i * 3 + 2] = 0; } l2[0] = 0; l2[1] = 0; l2[2] = 255;
    reset(0, 0, 0); glEnable(GL_TEXTURE_2D); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, l0); glTexImage2D(GL_TEXTURE_2D, 1, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, l1); glTexImage2D(GL_TEXTURE_2D, 2, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, l2);
    glColor3f(1, 1, 1); ok = 1;
    for (i = 0; i < 3; i++) {
        int r, g, b; reset(0, 0, 0); glEnable(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, i);
        glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(10, 10); glTexCoord2f(1, 0); glVertex2f(11, 10); glTexCoord2f(1, 1); glVertex2f(11, 11); glTexCoord2f(0, 1); glVertex2f(10, 11); glEnd();   /* 4 texels over 1 pixel: level 2 unless clamped */
        px(10, 10, &r, &g, &b); if (!(i == 0 ? (r > 200 && !g && !b) : i == 1 ? (g > 200 && !r && !b) : (b > 200 && !r && !g))) ok = 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000); glDisable(GL_TEXTURE_2D);
    aq("TEXTURE_MAX_LEVEL clamps the mipmap level (0,1,2)", glerr() ? -1 : ok, 0);
}

/* ---- pixel operations ---------------------------------------------------- */
static void a_pixel_transfer(void)
{
    static const unsigned char red[3] = { 255, 0, 0 }; int r, g, b, ok;
    reset(0, 0, 0); glPixelTransferf(GL_RED_SCALE, 0.5f); glPixelTransferf(GL_GREEN_BIAS, 0.25f); glRasterPos2i(40, 40); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, red);
    glPixelTransferf(GL_RED_SCALE, 1); glPixelTransferf(GL_GREEN_BIAS, 0); px(40, 40, &r, &g, &b); ok = abs(r - 128) < 12 && abs(g - 64) < 12 && b < 12;
    { char n[64]; snprintf(n, sizeof n, " %d,%d,%d (want 128,64,0)", r, g, b); strncat(g_act, n, sizeof g_act - strlen(g_act) - 1); }
    aq("pixel transfer (red scale 0.5, green bias 0.25)", glerr() ? -1 : ok, 0);
}
static void a_pixel_store(void)
{
    unsigned char d[4 * 3 * 3]; int x, y, ok, r, g, b; for (y = 0; y < 3; y++) for (x = 0; x < 4; x++) { d[(y * 4 + x) * 3] = x == 2 ? 255 : 0; d[(y * 4 + x) * 3 + 1] = y == 1 ? 255 : 0; d[(y * 4 + x) * 3 + 2] = 0; }
    reset(0, 0, 0); glPixelStorei(GL_UNPACK_ROW_LENGTH, 4); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2); glPixelStorei(GL_UNPACK_SKIP_ROWS, 1); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glRasterPos2i(40, 40); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, d);                                         /* element (x = 2, y = 1) = (255, 255, 0) */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    px(40, 40, &r, &g, &b); ok = r > 200 && g > 200 && b < 40;
    aq("pixel store (row length, skip pixels/rows)", glerr() ? -1 : ok, 0);
}
static void a_pixel_zoom_readback(void)
{
    static const unsigned char c[12] = { 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0 }; int ok; unsigned char v[4 * 3]; int i;
    reset(0, 0, 0); glPixelZoom(2, 2); glRasterPos2i(40, 40); glDrawPixels(2, 2, GL_RGB, GL_UNSIGNED_BYTE, c); glPixelZoom(1, 1);
    glFinish(); glReadPixels(40, 40, 4, 1, GL_RGB, GL_UNSIGNED_BYTE, v); ok = 1; for (i = 0; i < 4; i++) if (v[i * 3 + 1] < 200) ok = 0;
    aq("pixel zoom 2x + glReadPixels row", glerr() ? -1 : ok, 0);
}
static void a_draw_front(void)
{
    int r, g, b, ok; unsigned char v[3] = { 0, 0, 0 };
    reset(0, 0, 0); glDrawBuffer(GL_FRONT); quad(10, 10, 100, 100, 0, 1, 1, 1); glFinish(); glFlush(); glReadBuffer(GL_FRONT); glReadPixels(50, 50, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, v);
    glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK); r = v[0]; g = v[1]; b = v[2]; ok = r < 40 && g > 200 && b > 200;
    { char n[64]; snprintf(n, sizeof n, " front=%d,%d,%d", r, g, b); strncat(g_act, n, sizeof g_act - strlen(g_act) - 1); }
    aq("glDrawBuffer(GL_FRONT) + glReadBuffer(GL_FRONT)", glerr() ? -1 : ok, 0);
}
static void a_copypixels(void)
{
    static const unsigned char c[3] = { 200, 100, 50 }; int r, g, b, ok;
    reset(0, 0, 0); glRasterPos2i(20, 20); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, c);
    glPixelZoom(3, 3); glPixelTransferf(GL_RED_SCALE, 0.5f); glRasterPos2i(60, 60); glCopyPixels(20, 20, 1, 1, GL_COLOR); glPixelTransferf(GL_RED_SCALE, 1); glPixelZoom(1, 1);
    px(61, 61, &r, &g, &b); ok = abs(r - 100) < 12 && abs(g - 100) < 12 && abs(b - 50) < 12;
    px(62, 62, &r, &g, &b); ok = ok && abs(r - 100) < 12;           /* zoom 3 covers 60..62 */
    px(63, 63, &r, &g, &b); ok = ok && r < 12;
    aq("CopyPixels with zoom 3 and pixel transfer", glerr() ? -1 : ok, 0);
}
static void a_pixel_map(void)
{
    static const GLfloat inv[2] = { 1.0f, 0.0f }; static const unsigned char lo[3] = { 40, 200, 50 }; int r, g, b, ok;
    reset(0, 0, 0); glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 2, inv); glPixelTransferi(GL_MAP_COLOR, 1);
    glRasterPos2i(40, 40); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, lo); glRasterPos2i(60, 40); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, lo + 1);
    glPixelTransferi(GL_MAP_COLOR, 0);
    px(40, 40, &r, &g, &b); ok = r > 240 && g < 20 && b < 20;                /* red 40 -> index 0 -> 1.0; the default G/B maps (size 1, value 0) zero the others */
    px(60, 40, &r, &g, &b); ok = ok && r < 20;                                     /* red 200 -> index 1 -> 0.0 */
    aq("pixel maps (R_TO_R, MAP_COLOR)", glerr() ? -1 : ok, 0);
}
static void a_post_matrix(void)
{
    static const unsigned char c[3] = { 200, 100, 50 }; int r, g, b, ok;
    reset(0, 0, 0); glPixelTransferf(GL_POST_COLOR_MATRIX_RED_SCALE, 0.5f); glPixelTransferf(GL_POST_COLOR_MATRIX_GREEN_BIAS, 0.25f); glPixelTransferf(GL_POST_COLOR_MATRIX_BLUE_SCALE, 0.0f);
    glRasterPos2i(40, 40); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, c);
    glPixelTransferf(GL_POST_COLOR_MATRIX_RED_SCALE, 1); glPixelTransferf(GL_POST_COLOR_MATRIX_GREEN_BIAS, 0); glPixelTransferf(GL_POST_COLOR_MATRIX_BLUE_SCALE, 1);
    px(40, 40, &r, &g, &b); ok = abs(r - 100) < 12 && abs(g - 164) < 12 && b < 12;       /* 200*.5, 100+.25*255, 50*0 */
    aq("post-colour-matrix scale/bias", glerr() ? -1 : ok, 0);
}
static void a_color_matrix(void)
{
    static const unsigned char red[3] = { 255, 0, 0 }; int r, g, b, ok;
    reset(0, 0, 0); glMatrixMode(GL_COLOR); glLoadIdentity(); glScalef(0.5f, 1, 1); glMatrixMode(GL_MODELVIEW);
    glRasterPos2i(40, 40); glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, red);
    glMatrixMode(GL_COLOR); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); px(40, 40, &r, &g, &b); ok = abs(r - 128) < 14;
    aq("colour matrix (SGI_color_matrix / ARB_imaging)", glerr() ? -1 : ok, 0);
}

/* ---- lighting, selection, arrays (GLEngine side, through the driver) --------- */
static void a_two_side_light(void)
{
    GLfloat red[4] = { 1, 0, 0, 1 }, grn[4] = { 0, 1, 0, 1 }, amb[4] = { 0, 0, 0, 1 }, wh[4] = { 1, 1, 1, 1 }, pos[4] = { 0, 0, -1, 0 }; int ok;      /* the light shines on the BACK of the quad */
    reset(0, 0, 0); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glLightfv(GL_LIGHT0, GL_POSITION, pos); glLightfv(GL_LIGHT0, GL_DIFFUSE, wh); glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1); glMaterialfv(GL_FRONT, GL_DIFFUSE, red); glMaterialfv(GL_BACK, GL_DIFFUSE, grn); glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, amb);
    glDisable(GL_CULL_FACE); glNormal3f(0, 0, 1);
    glBegin(GL_QUADS); glVertex2f(10, 10); glVertex2f(10, 100); glVertex2f(100, 100); glVertex2f(100, 10); glEnd();                 /* clockwise: back face */
    ok = near3(50, 50, 0, 255, 0, 40); glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 0); glDisable(GL_LIGHTING);
    aq("two-sided lighting (back material)", glerr() ? -1 : ok, 0);
}
static void a_color_material(void)
{
    GLfloat wh[4] = { 1, 1, 1, 1 }, amb[4] = { 0, 0, 0, 1 }, pos[4] = { 0, 0, 1, 0 }; int ok;
    reset(0, 0, 0); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glLightfv(GL_LIGHT0, GL_POSITION, pos); glLightfv(GL_LIGHT0, GL_DIFFUSE, wh); glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE); glNormal3f(0, 0, 1); quad(10, 10, 100, 100, 0, 0, 1, 1);
    ok = near3(50, 50, 0, 0, 255, 40); glDisable(GL_COLOR_MATERIAL); glDisable(GL_LIGHTING);
    aq("colour material", glerr() ? -1 : ok, 0);
}
static void a_select(void)
{
    GLuint buf[64]; GLint hits;
    reset(0, 0, 0); glSelectBuffer(64, buf); glRenderMode(GL_SELECT); glInitNames(); glPushName(7); quad(10, 10, 100, 100, 1, 1, 1, 1); glPopName(); hits = glRenderMode(GL_RENDER);
    aq("selection mode (1 hit)", glerr() ? -1 : (hits == 1 && buf[3] == 7), 0);
}
static void a_feedback(void)
{
    GLfloat buf[256]; GLint n;
    reset(0, 0, 0); glFeedbackBuffer(256, GL_3D, buf); glRenderMode(GL_FEEDBACK); quad(10, 10, 100, 100, 1, 1, 1, 1); n = glRenderMode(GL_RENDER);
    aq("feedback mode (polygon token)", glerr() ? -1 : (n > 0 && buf[0] == GL_POLYGON_TOKEN), 0);
}
static void a_interleaved(void)
{
    static const GLfloat v[] = { 1, 0, 0, 10, 10, 0, 1, 0, 0, 100, 10, 0, 1, 0, 0, 100, 100, 0, 1, 0, 0, 10, 100, 0 }; static const GLubyte idx[6] = { 0, 1, 2, 0, 2, 3 }; int ok;
    reset(0, 0, 0); glInterleavedArrays(GL_C3F_V3F, 0, v); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, idx); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
    ok = near3(50, 50, 255, 0, 0, 30);
    aq("glInterleavedArrays + glDrawElements", glerr() ? -1 : ok, 0);
}
static void a_fog_modes(void)
{
    GLfloat fc[4] = { 0, 0, 1, 1 }; int ok = 1, k; static const GLenum modes[3] = { GL_LINEAR, GL_EXP, GL_EXP2 };
    for (k = 0; k < 3; k++) {
        int r, g, b;
        reset(0, 0, 0); glEnable(GL_FOG); glFogfv(GL_FOG_COLOR, fc); glFogi(GL_FOG_MODE, modes[k]); glFogf(GL_FOG_START, 0); glFogf(GL_FOG_END, 2); glFogf(GL_FOG_DENSITY, 0.5f);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, VW, 0, VW, 0, 4); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(0, 0, -1);   /* eye distance 1 */
        quad(10, 10, 100, 100, 1, 0, 0, 1); glDisable(GL_FOG); px(50, 50, &r, &g, &b);
        /* LINEAR (2-1)/2 = 0.5 -> r 128; EXP exp(-0.5) = 0.607 -> r 155; EXP2 exp(-0.25) = 0.779 -> r 199 */
        if (abs(r - (k == 0 ? 128 : k == 1 ? 155 : 199)) > 24 || abs(b - (k == 0 ? 128 : k == 1 ? 100 : 56)) > 30) ok = 0;
    }
    aq("fog LINEAR / EXP / EXP2", glerr() ? -1 : ok, 0);
}
static void a_depth_funcs(void)
{
    static const GLenum fn[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    /* incoming z = 0.0 -> window depth 0.5 on a plane at 0.0; stored depth 0.5 (equal), compare with a plane at +0.5 (nearer: smaller depth) and -0.5 (farther) */
    static const int expect[3][8] = { { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 0, 1, 1, 0, 0, 1, 1 }, { 0, 0, 0, 0, 1, 1, 1, 1 } }; static const float zz[3] = { 0.5f, 0.0f, -0.5f };   /* nearer, equal, farther (ortho -1..1: larger z is nearer) */
    int k, p, ok = 1; char bad[64] = "";
    for (k = 0; k < 8; k++) for (p = 0; p < 3; p++) {
        int r, g, b, passed;
        reset(0, 0, 0); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glClear(GL_DEPTH_BUFFER_BIT);
        glColor3f(1, 0, 0); glBegin(GL_QUADS); glVertex3f(10, 10, 0); glVertex3f(100, 10, 0); glVertex3f(100, 100, 0); glVertex3f(10, 100, 0); glEnd();
        glDepthFunc(fn[k]); glColor3f(0, 1, 0); glBegin(GL_QUADS); glVertex3f(10, 10, zz[p]); glVertex3f(100, 10, zz[p]); glVertex3f(100, 100, zz[p]); glVertex3f(10, 100, zz[p]); glEnd();
        px(50, 50, &r, &g, &b); passed = g > 128;
        if (passed != expect[p][k] && !bad[0]) snprintf(bad, sizeof bad, " func %d plane %d got %d want %d", k, p, passed, expect[p][k]);
        if (passed != expect[p][k]) ok = 0;
    }
    aq("depth functions (8 funcs x nearer/equal/farther)", glerr() ? -1 : ok, bad);
}
static void a_alpha_funcs(void)
{
    static const GLenum fn[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    static const float al[3] = { 0.25f, 0.5f, 0.75f }; static const int expect[3][8] = { { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 0, 1, 1, 0, 0, 1, 1 }, { 0, 0, 0, 0, 1, 1, 1, 1 } };
    int k, p, ok = 1; char bad[64] = "";
    for (k = 0; k < 8; k++) for (p = 0; p < 3; p++) {
        int r, g, b, passed;
        reset(0, 0, 0); glEnable(GL_ALPHA_TEST); glAlphaFunc(fn[k], 0.5f); quad(10, 10, 100, 100, 0, 1, 0, al[p]); glDisable(GL_ALPHA_TEST);
        px(50, 50, &r, &g, &b); passed = g > 128; if (passed != expect[p][k] && !bad[0]) snprintf(bad, sizeof bad, " func %d alpha %.2f got %d", k, al[p], passed);
        if (passed != expect[p][k]) ok = 0;
    }
    aq("alpha test functions (8 funcs x below/equal/above)", glerr() ? -1 : ok, bad);
}
static void a_logic_ops(void)
{
    static const GLenum op[8] = { GL_CLEAR, GL_AND, GL_OR, GL_XOR, GL_INVERT, GL_COPY_INVERTED, GL_SET, GL_NOOP }; int k, ok = 1; char bad[64] = "";
    static const int want[8] = { 0, 0xf0 & 0xcc, 0xf0 | 0xcc, 0xf0 ^ 0xcc, 0xcc ^ 0xff, 0xf0 ^ 0xff, 0xff, 0xcc };       /* source 0xf0 on destination 0xcc, red channel */
    for (k = 0; k < 8; k++) {
        int r, g, b; reset(0.8f, 0, 0); glEnable(GL_COLOR_LOGIC_OP); glLogicOp(op[k]); quad(10, 10, 100, 100, 240.0f / 255.0f, 0, 0, 1); glDisable(GL_COLOR_LOGIC_OP);
        px(50, 50, &r, &g, &b); if (abs((r & 0xf8) - (want[k] & 0xf8)) > 8 && !bad[0]) snprintf(bad, sizeof bad, " op %d got %d want %d", k, r, want[k]); if (abs((r & 0xf8) - (want[k] & 0xf8)) > 8) ok = 0;
    }
    aq("logic ops (8 of 16)", glerr() ? -1 : ok, bad);
}

/* ---- extensions ----------------------------------------------------------- */
#define STEP(n) do { GLenum e_ = glGetError(); if (e_) { char b_[40]; snprintf(b_, sizeof b_, " step%d:0x%x", n, e_); strncat(g_act, b_, sizeof g_act - strlen(g_act) - 1); } } while (0)
static void mt_run(const char *name, GLenum env0)
{
    unsigned char ta[8 * 8 * 3], tb[8 * 8 * 3]; int ok, i; GLuint a, b;
    for (i = 0; i < 64; i++) { int x = i & 7; ta[i * 3] = x < 4 ? 255 : 0; ta[i * 3 + 1] = x < 4 ? 0 : 255; ta[i * 3 + 2] = 0; tb[i * 3] = 128; tb[i * 3 + 1] = 255; tb[i * 3 + 2] = 255; }
    reset(0, 0, 0); STEP(0); a = mktex(GL_TEXTURE_2D, 8, 8, ta, GL_NEAREST, GL_CLAMP); (void)a; STEP(1);
    glActiveTextureARB(GL_TEXTURE1); STEP(2); glEnable(GL_TEXTURE_2D); b = mktex(GL_TEXTURE_2D, 8, 8, tb, GL_NEAREST, GL_CLAMP); (void)b; STEP(3); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); STEP(4);
    glActiveTextureARB(GL_TEXTURE0); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env0); STEP(5);
    glColor3f(1, 1, 1); glBegin(GL_QUADS);
    glMultiTexCoord2fARB(GL_TEXTURE0, 0, 0); glMultiTexCoord2fARB(GL_TEXTURE1, 0, 0); glVertex2f(10, 10); glMultiTexCoord2fARB(GL_TEXTURE0, 1, 0); glMultiTexCoord2fARB(GL_TEXTURE1, 1, 0); glVertex2f(100, 10);
    glMultiTexCoord2fARB(GL_TEXTURE0, 1, 1); glMultiTexCoord2fARB(GL_TEXTURE1, 1, 1); glVertex2f(100, 100); glMultiTexCoord2fARB(GL_TEXTURE0, 0, 1); glMultiTexCoord2fARB(GL_TEXTURE1, 0, 1); glVertex2f(10, 100); glEnd(); STEP(6);
    ok = near3(30, 50, 128, 0, 0, 24) && near3(80, 50, 0, 255, 0, 24);
    glActiveTextureARB(GL_TEXTURE1); glDisable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE0); glDisable(GL_TEXTURE_2D);
    aq(name, glerr() ? -1 : ok, 0);
}
static void a_multitexture(void) { mt_run("multitexture (unit 0 MODULATE, unit 1 MODULATE)", GL_MODULATE); }
static void a_multitexture_rep(void) { mt_run("multitexture (unit 0 REPLACE, unit 1 MODULATE)", GL_REPLACE); }
static void a_crossbar(void)
{
    static const unsigned char blue[3] = { 0, 0, 255 }, red[3] = { 255, 0, 0 }; GLuint a, b; int ok;
    reset(0, 0, 0); a = mktex(GL_TEXTURE_2D, 1, 1, red, GL_NEAREST, GL_CLAMP); (void)a;                  /* unit 0: red */
    glActiveTextureARB(GL_TEXTURE1); glEnable(GL_TEXTURE_2D); b = mktex(GL_TEXTURE_2D, 1, 1, blue, GL_NEAREST, GL_CLAMP); (void)b;     /* unit 1's own texture: blue */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB); glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE); glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE0_ARB);   /* crossbar: red */
    glActiveTextureARB(GL_TEXTURE0); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor3f(1, 1, 1); glBegin(GL_QUADS); glMultiTexCoord2fARB(GL_TEXTURE0, 0, 0); glMultiTexCoord2fARB(GL_TEXTURE1, 0, 0); glVertex2f(10, 10); glMultiTexCoord2fARB(GL_TEXTURE0, 1, 0); glMultiTexCoord2fARB(GL_TEXTURE1, 1, 0); glVertex2f(100, 10);
    glMultiTexCoord2fARB(GL_TEXTURE0, 1, 1); glMultiTexCoord2fARB(GL_TEXTURE1, 1, 1); glVertex2f(100, 100); glMultiTexCoord2fARB(GL_TEXTURE0, 0, 1); glMultiTexCoord2fARB(GL_TEXTURE1, 0, 1); glVertex2f(10, 100); glEnd();
    ok = near3(50, 50, 255, 0, 0, 24);
    glActiveTextureARB(GL_TEXTURE1); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glDisable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE0); glDisable(GL_TEXTURE_2D);
    aq("texture_env_crossbar (unit 1 reads TEXTURE0)", glerr() ? -1 : ok, 0);
}
static void a_blend_eq_sep(void)
{
    int ok; reset(0.5f, 0.5f, 0.5f); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glBlendEquationSeparateEXT(GL_FUNC_SUBTRACT_EXT, GL_FUNC_ADD_EXT); quad(10, 10, 100, 100, 0.75f, 0.5f, 0.25f, 1);
    glBlendEquationEXT(GL_FUNC_ADD_EXT); ok = near3(50, 50, 64, 0, 0, 24);         /* src - dst: (0.75,0.5,0.25) - 0.5 */
    aq("blend_equation_separate (RGB subtract)", glerr() ? -1 : ok, 0);
}
static void a_vbo(void)
{
    static const float v[8] = { 10, 10, 100, 10, 100, 100, 10, 100 }; GLuint buf = 0; int ok;
    reset(0, 0, 0); glGenBuffersARB(1, &buf); glBindBufferARB(GL_ARRAY_BUFFER_ARB, buf); glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof v, v, GL_STATIC_DRAW_ARB);
    glColor3f(1, 0, 1); glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(2, GL_FLOAT, 0, 0); glDrawArrays(GL_QUADS, 0, 4); glDisableClientState(GL_VERTEX_ARRAY);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0); glDeleteBuffersARB(1, &buf); ok = near3(50, 50, 255, 0, 255, 20);
    aq("ARB_vertex_buffer_object", glerr() ? -1 : ok, 0);
}
static void a_cva(void)
{
    static const float v[8] = { 10, 10, 100, 10, 100, 100, 10, 100 }; int ok;
    reset(0, 0, 0); glColor3f(0, 1, 1); glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(2, GL_FLOAT, 0, v); glLockArraysEXT(0, 4); glDrawArrays(GL_QUADS, 0, 4); glUnlockArraysEXT(); glDisableClientState(GL_VERTEX_ARRAY);
    ok = near3(50, 50, 0, 255, 255, 20);
    aq("EXT_compiled_vertex_array", glerr() ? -1 : ok, 0);
}
static void a_point_atten(void)
{
    GLfloat att[3] = { 1, 0, 100 }; int big, small;
    reset(0, 0, 0); glPointSize(8); glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, att); glColor3f(1, 1, 1);
    glBegin(GL_POINTS); glVertex3f(30, 64, 0); glVertex3f(90, 64, -0.9f); glEnd();                                    /* eye distance 0: size 8; 0.9: 8 / sqrt(1 + 100 * 0.81) = 0.9 */
    big = lit(30, 64) && lit(33, 67) && lit(27, 61); small = !lit(93, 64) && !lit(90, 67);
    att[0] = 1; att[1] = 0; att[2] = 0; glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, att);
    aq("point parameters: distance attenuation changes size", glerr() ? -1 : (big && small), big ? "" : "near point not size 8");
}
static void a_point_fade(void)
{
    GLfloat att[3] = { 1, 0, 100 }; int dim, full;
    reset(0, 0, 0); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glPointSize(8); glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, att);
    glPointParameterfARB(GL_POINT_FADE_THRESHOLD_SIZE_ARB, 4.0f); glPointParameterfARB(GL_POINT_SIZE_MIN_ARB, 0.0f); glColor4f(1, 1, 1, 1);
    glBegin(GL_POINTS); glVertex3f(30, 64, 0); glVertex3f(90, 64, -0.5f); glEnd();                           /* near: size 8 (opaque); d 0.5: 8/sqrt(26) = 1.57 -> alpha (1.57/4)^2 = 0.15 */
    glDisable(GL_BLEND); att[2] = 0; glPointParameterfvARB(GL_POINT_DISTANCE_ATTENUATION_ARB, att); glPointParameterfARB(GL_POINT_FADE_THRESHOLD_SIZE_ARB, 1.0f);
    { int r, g, b; px(30, 64, &r, &g, &b); full = r > 240; px(90, 64, &r, &g, &b); dim = r > 10 && r < 90; }
    aq("point parameters: fade threshold dims small points", glerr() ? -1 : (full && dim), 0);
}
static void a_depth_texture(void)
{
    static const float d[4] = { 0.1f, 0.4f, 0.7f, 0.9f }; GLuint id; int e;
    reset(0, 0, 0); glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 2, 2, 0, GL_DEPTH_COMPONENT, GL_FLOAT, d); e = glerr();
    aq("ARB_depth_texture (upload accepted)", e ? -1 : 1, 0);
}
static void a_multisample_attr(void)
{
    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_SAMPLE_BUFFERS_ARB, 1, AGL_SAMPLES_ARB, 4, AGL_NONE }; AGLPixelFormat pf = aglChoosePixelFormat(0, 0, attrs);
    aq("ARB_multisample (4x pixel format offered)", pf ? 1 : -1, pf ? "" : "no multisample pixel format"); if (pf) aglDestroyPixelFormat(pf);
}

#define AUDIT_ENTRIES \
    { "wide_point", a_wide_point, 0 }, { "wide_line", a_wide_line, 0 }, { "polygon_stipple", a_polygon_stipple, 0 }, { "polygon_smooth", a_polygon_smooth, 0 }, \
    { "polyoffset_line", a_polyoffset_line, 0 }, { "polyoffset_point", a_polyoffset_point, 0 }, { "tex1d", a_tex1d, 0 }, { "tex_border", a_tex_border, 0 }, { "tex_border_rep", a_tex_border_rep, 0 }, \
    { "texgen", a_texgen, 0 }, { "texgen_sphere", a_texgen_sphere, 0 }, { "texmatrix", a_texmatrix, 0 }, { "texsubimage", a_texsubimage, 0 }, { "texsubimage1d", a_texsubimage1d, 0 }, \
    { "tex_objects", a_tex_objects, 0 }, { "tex_internal", a_tex_internal, 0 }, { "tex_bgra", a_tex_bgra, "GL_EXT_bgra" }, { "tex_abgr", a_tex_abgr, "GL_EXT_abgr" }, \
    { "tex_maxlevel", a_tex_maxlevel, 0 }, { "pixel_transfer", a_pixel_transfer, 0 }, { "pixel_store", a_pixel_store, 0 }, { "pixel_zoom", a_pixel_zoom_readback, 0 }, \
    { "draw_front", a_draw_front, 0 }, { "color_matrix", a_color_matrix, "GL_SGI_color_matrix" }, { "post_matrix", a_post_matrix, "GL_SGI_color_matrix" }, { "pixel_map", a_pixel_map, 0 }, { "copypixels", a_copypixels, 0 }, { "two_side_light", a_two_side_light, 0 }, { "color_material", a_color_material, 0 }, \
    { "select", a_select, 0 }, { "feedback", a_feedback, 0 }, { "interleaved", a_interleaved, 0 }, { "fog_modes", a_fog_modes, 0 }, { "depth_funcs", a_depth_funcs, 0 }, \
    { "alpha_funcs", a_alpha_funcs, 0 }, { "logic_ops", a_logic_ops, 0 }, { "multitexture", a_multitexture, "GL_ARB_multitexture" }, { "multitexture_rep", a_multitexture_rep, "GL_ARB_multitexture" }, { "crossbar", a_crossbar, "GL_ARB_texture_env_crossbar" }, \
    { "blend_eq_sep", a_blend_eq_sep, "GL_EXT_blend_equation_separate" }, { "vbo", a_vbo, "GL_ARB_vertex_buffer_object" }, { "cva", a_cva, "GL_EXT_compiled_vertex_array" }, \
    { "point_atten", a_point_atten, "GL_ARB_point_parameters" }, { "point_fade", a_point_fade, "GL_ARB_point_parameters" }, { "depth_texture", a_depth_texture, "GL_ARB_depth_texture" }, { "multisample", a_multisample_attr, "GL_ARB_multisample" },
