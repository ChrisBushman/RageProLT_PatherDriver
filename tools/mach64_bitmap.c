/* mach64_bitmap.c - verify glBitmap.  Fills the framebuffer with a background (a fullscreen quad, so
 * our lazy clear fires), sets the raster colour to YELLOW, then glBitmap a 16x16 mask whose bottom-left
 * 8x8 bits are SET (rest clear).  glReadPixels checks: the set region is yellow (confirms the raster
 * colour source + 1-bpp decode), and the clear region + elsewhere is background (confirms bit test +
 * X/Y orientation: bits are MSB-first, rows bottom-to-top).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void rd(int x, int y, const char *what)
{
    GLubyte px[4];
    px[0]=1; px[1]=2; px[2]=3; px[3]=4;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-13s (%d,%d) = %3d %3d %3d\n", what, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLint fq[8];
    GLubyte bm[32];               /* 16 wide -> 2 bytes/row, 16 rows */
    int i, row;

    fq[0]=0; fq[1]=0; fq[2]=1024; fq[3]=0; fq[4]=1024; fq[5]=768; fq[6]=0; fq[7]=768;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* fill background with a fullscreen quad in the bg colour (forces our lazy clear + a known bg) */
    glColor3f(0.25f, 0.5f, 0.75f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_INT, 0, fq);
    glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    /* 16x16 mask: bottom-left 8x8 set (row 0 = bottom, MSB-first) */
    for (i = 0; i < 32; i++) bm[i] = 0;
    for (row = 0; row < 8; row++) bm[row*2] = 0xFF;   /* bottom 8 rows, left byte (cols 0-7) */

    glColor3f(1.0f, 1.0f, 0.0f);                      /* raster colour -> YELLOW */
    glRasterPos2i(400, 300);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBitmap(16, 16, 0.0f, 0.0f, 0.0f, 0.0f, bm);
    glFinish();

    rd(403, 303, "set(yellow)");   /* bitmap (3,3) set  -> expect 255 255   0 */
    rd(403, 312, "top(clear)");    /* bitmap (3,12) clr -> expect  64 128 191 */
    rd(412, 303, "right(clear)");  /* bitmap (12,3) clr -> expect  64 128 191 */
    rd(200, 200, "bg");            /* far away          -> expect  64 128 191 */
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    printf("expect: set=255,255,0  clear/bg=64,128,191\n");
    fflush(stdout);
    sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
