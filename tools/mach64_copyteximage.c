/* mach64_copyteximage.c - glCopyTexImage2D correctness incl. VERTICAL ORIENTATION.
 * GLEngine decomposes glCopyTexImage2D into our framebuffer readback + our texture upload (confirmed
 * via the driver log: ">>> READBACK 64x64" then "mach64 texture upload: 64x64").  A solid colour
 * proved the colour path; this checks the copy is not vertically FLIPPED by using a split source:
 *   source region: BOTTOM half (screen) = green, TOP half = red.
 * Copy it to a texture, draw a textured quad, and read the quad's bottom + top.  The textured quad
 * must match the source (bottom green, top red); swapped = a Y-flip bug in the readback/upload compose.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=px[1]=px[2]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-18s (%d,%d) = R%3d G%3d B%3d\n", w, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLuint tex;
    GLfloat sbot[8], stop[8], dst[8], tc[8];

    /* source region (100,100)-(164,164) [ortho y-up]: lower half y100..132 green, upper half y132..164 red */
    sbot[0]=100;sbot[1]=100; sbot[2]=164;sbot[3]=100; sbot[4]=164;sbot[5]=132; sbot[6]=100;sbot[7]=132;
    stop[0]=100;stop[1]=132; stop[2]=164;stop[3]=132; stop[4]=164;stop[5]=164; stop[6]=100;stop[7]=164;
    dst[0]=400;dst[1]=300; dst[2]=464;dst[3]=300; dst[4]=464;dst[5]=364; dst[6]=400;dst[7]=364;
    tc[0]=0;tc[1]=0; tc[2]=1;tc[3]=0; tc[4]=1;tc[5]=1; tc[6]=0;tc[7]=1;   /* t=0 at bottom vertex */

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);

    /* 1. render the split source: bottom green, top red */
    glDisable(GL_TEXTURE_2D);
    glColor3f(0, 1, 0); glVertexPointer(2, GL_FLOAT, 0, sbot); glDrawArrays(GL_QUADS, 0, 4);
    glColor3f(1, 0, 0); glVertexPointer(2, GL_FLOAT, 0, stop); glDrawArrays(GL_QUADS, 0, 4);
    glFinish();
    rd(132, 110, "src bottom(green)");    /* low y  -> expect green */
    rd(132, 154, "src top(red)");         /* high y -> expect red   */

    /* 2. copy the framebuffer region into a texture */
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 100, 100, 64, 64, 0);
    printf("glGetError after CopyTexImage = 0x%x\n", (unsigned)glGetError());

    /* 3. draw a textured quad elsewhere */
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glVertexPointer(2, GL_FLOAT, 0, dst); glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    /* 4. read the textured quad bottom + top -- must match the source (bottom green, top red) */
    rd(432, 310, "copytex bottom");       /* expect green if orientation preserved */
    rd(432, 354, "copytex top");          /* expect red   if orientation preserved */
    printf("PASS if copytex bottom=green & top=red (matches source).  Swapped = Y-flip bug.\n");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(5);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
