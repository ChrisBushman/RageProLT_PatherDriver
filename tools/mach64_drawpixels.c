/* mach64_drawpixels.c - verify glDrawPixels (GL_RGBA/GL_UNSIGNED_BYTE).  Uploads a 64x64 client image
 * with a distinctive layout -- left half RED, right half GREEN, bottom 8 rows BLUE (image row 0 = the
 * BOTTOM row in GL) -- to the raster position (300,300), then glReadPixels to check placement AND
 * orientation (X: red|green, Y: blue at the bottom).
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
    static GLubyte img[64*64*4];
    GLboolean rvalid = 0;
    int x, y, i;

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

    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        i = (y * 64 + x) * 4;
        if (y < 8)       { img[i]=0;   img[i+1]=0;   img[i+2]=255; }  /* bottom rows -> blue */
        else if (x < 32) { img[i]=255; img[i+1]=0;   img[i+2]=0;   }  /* left  -> red   */
        else             { img[i]=0;   img[i+1]=255; img[i+2]=0;   }  /* right -> green */
        img[i+3] = 255;
    }

    glRasterPos2i(300, 300);
    glGetBooleanv(GL_CURRENT_RASTER_POSITION_VALID, &rvalid);
    printf("raster pos (300,300) valid = %d\n", (int)rvalid);
    glDrawPixels(64, 64, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glFinish();

    rd(315, 340, "left(red)");     /* img (15,40): expect 255   0   0 */
    rd(350, 340, "right(green)");  /* img (50,40): expect   0 255   0 */
    rd(315, 303, "bottom(blue)");  /* img (15, 3): expect   0   0 255 */
    rd(315, 360, "top(red)");      /* img (15,60): expect 255   0   0 */
    rd(500, 500, "bg");            /* untouched:   expect  64 128 191 */
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    printf("expect: left=255,0,0 right=0,255,0 bottom=0,0,255 top=255,0,0 bg=64,128,191\n");
    fflush(stdout);
    sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
