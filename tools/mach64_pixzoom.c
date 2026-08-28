/* mach64_pixzoom.c - glPixelZoom + GL_UNPACK_SWAP_BYTES for glDrawPixels.
 * A 2x2 RGBA image (red/green/blue/white) is drawn three ways:
 *   zoom (1,1) at (100,100)  -> 2x2, one pixel each (baseline, no regression)
 *   zoom (4,4) at (200,100)  -> each source pixel becomes a 4x4 block (8x8 total)
 *   SWAP_BYTES packed pixel  -> one GL_UNSIGNED_INT_8_8_8_8 pixel 0x10203040 with UNPACK_SWAP_BYTES:
 *                               bytes reverse before extraction -> reads (0x40,0x30,0x20)=(64,48,32)
 * Reads back check each.  The driver "glDrawPixels: ... zoom=X,Y" log confirms the zoom offset read.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GL_UNSIGNED_INT_8_8_8_8
#define GL_UNSIGNED_INT_8_8_8_8 0x8035
#endif
#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES 0x0CF0
#endif

static void rd(int x, int y, const char *w)
{
    GLubyte px[4]; px[0]=px[1]=px[2]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-16s (%d,%d) = %3d %3d %3d\n", w, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLubyte img[2*2*4];
    GLuint  pix = 0x10203040u;

    /* row 0 (bottom): red, green ; row 1 (top): blue, white */
    img[0]=255;img[1]=0;img[2]=0;img[3]=255;      img[4]=0;img[5]=255;img[6]=0;img[7]=255;
    img[8]=0;img[9]=0;img[10]=255;img[11]=255;    img[12]=255;img[13]=255;img[14]=255;img[15]=255;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glPixelZoom(1, 1);  glRasterPos2i(100, 100);
    glDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, img);

    glPixelZoom(4, 4);  glRasterPos2i(200, 100);
    glDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, img);

    glPixelZoom(1, 1);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);  glRasterPos2i(400, 100);
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, &pix);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
    glFinish();

    printf("-- zoom 1 (baseline, 2x2) --\n");
    rd(100,100,"z1 red"); rd(101,100,"z1 green"); rd(100,101,"z1 blue"); rd(101,101,"z1 white");
    printf("-- zoom 4 (each -> 4x4 block) --\n");
    rd(202,102,"z4 red"); rd(206,102,"z4 green"); rd(202,106,"z4 blue"); rd(206,106,"z4 white");
    rd(210,102,"z4 past-edge");   /* should be background blue (image is only 8 wide from x=200) */
    printf("-- SWAP_BYTES packed pixel --\n");
    rd(400,100,"swap=(64,48,32)");
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(4);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
