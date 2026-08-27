/* mach64_readpixels.c - does glReadPixels work?  Discovery + verify.
 *
 * Renders a known pattern (clear to a distinct colour + a white centre quad), then
 * glReadPixels a pixel from the CLEAR region and a pixel from the QUAD, and prints both.
 *   clear px == (64,128,191) and quad px == (255,255,255)  -> glReadPixels WORKS.
 *   zeros / garbage / wrong                                 -> a readback path is needed
 *                                                              (see which gld* entry fires).
 * glReadPixels uses GL's lower-left origin, so y is measured from the BOTTOM.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_BGR
#define GL_BGR  0x80E0
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8
#define GL_UNSIGNED_INT_8_8_8_8     0x8035
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

/* Read one pixel as a packed uint32 and print the value.  At clear (R,G,B,A = 40,80,BF,FF hex):
 *   BGRA + INT_8_8_8_8_REV -> 0xFF4080BF (ARGB, the fast native form)
 *   RGBA + INT_8_8_8_8     -> 0x4080BFFF
 *   RGBA + INT_8_8_8_8_REV -> 0xFFBF8040    BGRA + INT_8_8_8_8 -> 0xBF8040FF */
static void rd_packed(int x, int y, unsigned fmt, unsigned type, const char *name)
{
    GLuint v = 0x99999999u;
    glReadPixels(x, y, 1, 1, fmt, type, &v);
    printf("READPIX %-16s (%d,%d) = 0x%08x\n", name, x, y, (unsigned)v);
}

static void rd(int x, int y, const char *what)
{
    GLubyte px[4] = { 1,2,3,4 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READPIX %-10s (%d,%d) = %3d %3d %3d %3d\n", what, x, y, px[0],px[1],px[2],px[3]);
}

/* Read one pixel in a given format (GL_UNSIGNED_BYTE) and print the bytes.  At the clear pixel
 * (R,G,B = 64,128,191) the expected byte order per format is:
 *   RGBA -> 64 128 191 255   BGRA -> 191 128 64 255   RGB -> 64 128 191 (-)   BGR -> 191 128 64 (-) */
static void rd_fmt(int x, int y, unsigned fmt, const char *fname)
{
    GLubyte px[4] = { 9,9,9,9 };
    glReadPixels(x, y, 1, 1, fmt, GL_UNSIGNED_BYTE, px);
    printf("READPIX %-6s (%d,%d) = %3d %3d %3d %3d\n", fname, x, y, px[0],px[1],px[2],px[3]);
}

/* Read a wxh block and print a compact map (Q=quad white, .=clear, ?=other, _=not written).
 * Placed straddling the quad's LEFT edge (screen x~=358): left half should be '.', right half 'Q',
 * a sharp vertical edge at the same column on every row -> confirms per-row stride + w>1 packing. */
static void rd_block(int x, int y, int w, int h)
{
    static GLubyte buf[64*64*4];
    int i, j, nq=0, nc=0, no=0;
    for (i = 0; i < w*h*4; i++) buf[i] = 0xAA;              /* sentinel = "not written" */
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    printf("BLOCK %dx%d at (%d,%d)  [Q=quad .=clear ?=other _=unwritten], top row first:\n", w, h, x, y);
    for (j = h-1; j >= 0; j -= 4) {                         /* GL row 0 = bottom; print top-down */
        printf("  ");
        for (i = 0; i < w; i += 2) {
            GLubyte *p = &buf[(j*w + i)*4]; char c;
            if      (p[0]==255 && p[1]==255 && p[2]==255) c='Q';
            else if (p[0]==64  && p[1]==128 && p[2]==191) c='.';
            else if (p[0]==0xAA && p[1]==0xAA && p[2]==0xAA) c='_';
            else c='?';
            putchar(c);
        }
        putchar('\n');
    }
    for (j = 0; j < w*h; j++) { GLubyte *p=&buf[j*4];
        if (p[0]==255&&p[1]==255&&p[2]==255) nq++;
        else if (p[0]==64&&p[1]==128&&p[2]==191) nc++; else no++; }
    printf("  block totals: quad=%d clear=%d other/unwritten=%d (of %d)\n", nq, nc, no, w*h);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLfloat q[8] = { -0.3f,-0.3f, 0.3f,-0.3f, 0.3f,0.3f, -0.3f,0.3f };

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);

    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);   /* -> ~ (64,128,191) */
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, q);
    glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    rd( 80,  80, "clear");    /* corner -> clear colour        */
    rd(512, 384, "quad");     /* centre -> white quad          */
    /* format coverage at the clear pixel (64,128,191): verify byte order per format */
    rd_fmt(80, 80, GL_RGBA, "RGBA");   /* expect  64 128 191 255 */
    rd_fmt(80, 80, GL_BGRA, "BGRA");   /* expect 191 128  64 255 */
    rd_fmt(80, 80, GL_RGB,  "RGB");    /* expect  64 128 191   9 (px[3] untouched) */
    rd_fmt(80, 80, GL_BGR,  "BGR");    /* expect 191 128  64   9 */
    /* packed int types at clear (0x40,0x80,0xBF, A=0xFF) */
    rd_packed(80, 80, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, "BGRA/8888REV"); /* 0xFF4080BF */
    rd_packed(80, 80, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,     "RGBA/8888");    /* 0x4080BFFF */
    rd_packed(80, 80, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, "RGBA/8888REV"); /* 0xFFBF8040 */
    rd_packed(80, 80, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8,     "BGRA/8888");    /* 0xBF8040FF */
    /* 64x64 straddling the quad's left edge (screen x~=358): expect left cols clear, right Q,
     * sharp edge at output col ~16 (i.e. screen x 358 -> block col 32, sampled i+=2). */
    rd_block(326, 350, 64, 64);
    printf("glGetError after readpixels = 0x%x (0=no error; 0x502=GL_INVALID_OPERATION)\n",
           (unsigned)glGetError());
    printf("expect clear=(64,128,191,*) quad=(255,255,255,*); block edge ~col 16\n");
    fflush(stdout);
    sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
