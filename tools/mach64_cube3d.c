/* mach64_cube3d.c - a textured, depth-buffered 3D scene: the first real "3D scene" test.
 *
 * A perspective-projected cube, checkerboard-textured on every face, rotated so three faces
 * show.  Exercises the whole textured-3D pipeline at once:
 *   - perspective projection (glFrustum) + GLEngine T&L -> post-transform verts with W
 *   - PERSPECTIVE-CORRECT texturing (checker squares foreshorten toward far edges; if the
 *     mach64's TEX_ST_MULT_W perspective divide were wrong they'd stay uniform / "swim")
 *   - DEPTH buffering (GL_DEPTH_TEST): the far faces must be hidden -- a solid cube, not a
 *     see-through wireframe of overlapping faces
 *   - texture wrap/filter + MODULATE env on transformed geometry
 *
 * PASS: a solid checkerboarded cube, three faces visible, squares perspective-foreshortened,
 *       no back faces bleeding through, clean edges between faces.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

/* 8 cube corners */
static const float C[8][3] = {
    {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
    {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1},
};
/* 6 faces, CCW from outside, as 4 corner indices */
static const int F[6][4] = {
    {4,5,6,7},  /* front  z=+1 */
    {1,0,3,2},  /* back   z=-1 */
    {5,1,2,6},  /* right  x=+1 */
    {0,4,7,3},  /* left   x=-1 */
    {7,6,2,3},  /* top    y=+1 */
    {0,1,5,4},  /* bottom y=-1 */
};
static const float TC[4][2] = { {0,0},{1,0},{1,1},{0,1} };  /* per face-corner texcoords */

static float vtx[36*3], tex[36*2];   /* 6 faces x 2 tris x 3 verts */

static void build_cube(void)
{
    int f, t, c, k = 0;
    static const int tri[2][3] = { {0,1,2}, {0,2,3} };   /* quad -> 2 tris */
    for (f = 0; f < 6; f++)
        for (t = 0; t < 2; t++)
            for (c = 0; c < 3; c++) {
                int corner = tri[t][c];
                int ci = F[f][corner];
                vtx[k*3+0] = C[ci][0]; vtx[k*3+1] = C[ci][1]; vtx[k*3+2] = C[ci][2];
                tex[k*2+0] = TC[corner][0]; tex[k*2+1] = TC[corner][1];
                k++;
            }
}

/* 64x64 checkerboard, 8px cells, white / dark-blue (crisp cells show perspective). */
static GLubyte checker[64*64*3];
static void build_texture(void)
{
    int x, y;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        int on = ((x >> 3) ^ (y >> 3)) & 1;
        GLubyte *p = &checker[(y*64 + x)*3];
        p[0] = on ? 255 : 20; p[1] = on ? 255 : 40; p[2] = on ? 255 : 120;
    }
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;

    build_cube();
    build_texture();

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glEnable(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, checker);
    glColor3f(1, 1, 1);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.75, 0.75, 2.0, 20.0);   /* ~53 deg FOV, 4:3 */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(25.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(35.0f, 0.0f, 1.0f, 0.0f);

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vtx);
    glTexCoordPointer(2, GL_FLOAT, 0, tex);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    glFlush(); glFinish();
    printf("drew a textured, depth-buffered cube (36 verts)\n");
    fflush(stdout);
    sleep(8);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
