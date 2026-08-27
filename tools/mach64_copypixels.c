/* mach64_copypixels.c - verify glCopyPixels (GL_COLOR).  Renders a white quad at (100,100)-(200,200)
 * on a coloured background, sets the raster position to (400,400), glCopyPixels the quad region there,
 * then glReadPixels to check: the copied region is white and the background elsewhere is untouched.
 *   copied (450,450) == 255,255,255  AND  bg (300,300) == 64,128,191  -> glCopyPixels WORKS.
 * Uses an ortho projection so object coords == window pixels (raster pos + quad in pixel space).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef GL_CURRENT_RASTER_POSITION_VALID
#define GL_CURRENT_RASTER_POSITION_VALID 0x0B08
#endif

static void rd(int x, int y, const char *what)
{
    GLubyte px[4];
    px[0]=9; px[1]=9; px[2]=9; px[3]=9;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("READ %-12s (%d,%d) = %3d %3d %3d\n", what, x, y, px[0], px[1], px[2]);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLint quad[8];
    GLboolean rvalid = 0;

    quad[0]=100; quad[1]=100; quad[2]=200; quad[3]=100;
    quad[4]=200; quad[5]=200; quad[6]=100; quad[7]=200;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);

    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);        /* bg ~ (64,128,191) */
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_INT, 0, quad);
    glDrawArrays(GL_QUADS, 0, 4);                  /* white quad (100,100)-(200,200) */
    glFinish();

    glRasterPos2i(400, 400);                       /* destination raster position */
    glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID, &rvalid);
    printf("raster pos (400,400) valid = %d\n", (int)rvalid);
    glCopyPixels(100, 100, 100, 100, GL_COLOR);    /* copy the quad region -> (400,400) */
    glFinish();

    rd(150, 150, "orig quad");     /* expect 255 255 255 (source still there) */
    rd(450, 450, "copied quad");   /* expect 255 255 255 if my Y-flip is right */
    rd(300, 300, "bg");            /* expect  64 128 191 (untouched) */

    /* LOCATE all white pixels on a coarse grid -> bounding box(es).  Shows where BOTH squares
     * actually landed (orig should be ~[100..200], copy wherever the blit really put it). */
    {   int gx, gy, minx=9999, miny=9999, maxx=-1, maxy=-1, nwhite=0;
        int cminx=9999, cminy=9999, cmaxx=-1, cmaxy=-1;   /* just the copy region (x>=300) */
        GLubyte px[4];
        for (gy = 0; gy < 768; gy += 12) {
            for (gx = 0; gx < 1024; gx += 12) {
                glReadPixels(gx, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                if (px[0] > 200 && px[1] > 200 && px[2] > 200) {
                    nwhite++;
                    if (gx<minx)minx=gx; if (gx>maxx)maxx=gx;
                    if (gy<miny)miny=gy; if (gy>maxy)maxy=gy;
                    if (gx >= 300) { if(gx<cminx)cminx=gx; if(gx>cmaxx)cmaxx=gx;
                                     if(gy<cminy)cminy=gy; if(gy>cmaxy)cmaxy=gy; }
                }
            }
        }
        printf("WHITE pixels: %d total, overall GL bbox x[%d..%d] y[%d..%d]\n",
               nwhite, minx, maxx, miny, maxy);
        if (cmaxx >= 0)
            printf("COPY square (x>=300) GL bbox x[%d..%d] y[%d..%d]  (expected x[400..500] y[400..500])\n",
                   cminx, cmaxx, cminy, cmaxy);
        else
            printf("COPY square: NONE found with x>=300 (copy landed elsewhere or not readable)\n");
    }
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout);
    sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
