/* mach64_texmin.c - verify the MINIFICATION filter (TEX_BLEND_FCN_LINEAR, bit26 = 0x04000000).
 * On mach64 the minification bilinear is a SEPARATE bit from BILINEAR_TEX_EN (mag-only), so BOTH
 * quads use mag=NEAREST to take BILINEAR_TEX_EN out of the picture and isolate the min filter:
 *   LEFT  = GL_LINEAR  min -> TEX_BLEND_FCN_LINEAR -> 2x2 bilinear tap when minified
 *   RIGHT = GL_NEAREST min -> point sample
 * KEY test-design point: an earlier version repeated a tiny 2x2 texture to huge texcoords (0..96) to
 * force minification -- but mach64 texcoord precision truncates the fractional phase at large values,
 * collapsing every pixel to one fixed texel (uniform output, no signal).  Instead: a LARGE 128x128
 * 1-pixel checkerboard drawn to a 64px quad = 2:1 minification with texcoords staying in 0..1.
 * A 1px checkerboard is the ideal probe:
 *   LINEAR min -> every 2x2 tap = 2 black + 2 white -> uniform ~128 grey (intermediate = all)
 *   NEAREST min -> every sample a pure texel -> hard 0 / 255 (intermediate = 0)
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#define TS 16              /* 16x16 = 1KB texture: fits the 4KB texture cache -> bilinear-min safe.
                            * (128x128 = 64KB overflowed the cache and wedged the minify-fetch path.) */
static GLubyte checker[TS*TS*4];   /* 1px checkerboard */

static int scan(int x0, int y, int n, int step, const char *tag)
{
    int i, inter = 0, lo = 255, hi = 0;
    printf("%-14s:", tag);
    for (i = 0; i < n; i++) {
        GLubyte px[4]; px[0] = 0;
        glReadPixels(x0 + i*step, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        printf(" %3d", px[0]);
        if (px[0] > 16 && px[0] < 239) inter++;
        if (px[0] < lo) lo = px[0];
        if (px[0] > hi) hi = px[0];
    }
    printf("   | intermediate=%d/%d  range=%d..%d\n", inter, n, lo, hi);
    return inter;
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLuint tl, tn;
    GLfloat v1[8], v2[8], tc[8];
    int x, y, il, ir;

    for (y = 0; y < TS; y++)          /* 1px checkerboard: black/white by (x+y) parity */
        for (x = 0; x < TS; x++) {
            GLubyte c = ((x + y) & 1) ? 255 : 0;
            GLubyte *p = &checker[(y*TS + x)*4];
            p[0]=p[1]=p[2]=c; p[3]=255;
        }
    /* two 48x48 quads; texcoords 0..8 tile the 16px checker -> 16*8/48 = 2.67 texels/pixel (LOD~1.4,
     * STRONG minification to force the min-filter path -- rules out "LOD too mild") while staying LOW
     * enough to avoid the mach64 texcoord-precision truncation that collapsed the 0..96 / 0..256 runs. */
    v1[0]=140;v1[1]=300; v1[2]=188;v1[3]=300; v1[4]=188;v1[5]=348; v1[6]=140;v1[7]=348;   /* LEFT  */
    v2[0]=540;v2[1]=300; v2[2]=588;v2[3]=300; v2[4]=588;v2[5]=348; v2[6]=540;v2[7]=348;   /* RIGHT */
    tc[0]=0;tc[1]=0; tc[2]=8;tc[3]=0; tc[4]=8;tc[5]=8; tc[6]=0;tc[7]=8;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1024, 0, 768, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glClearColor(0, 0, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, tc);

    /* LEFT: min=LINEAR, mag=NEAREST -> only TEX_BLEND_FCN_LINEAR under test */
    glGenTextures(1, &tl); glBindTexture(GL_TEXTURE_2D, tl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TS, TS, 0, GL_RGBA, GL_UNSIGNED_BYTE, checker);
    glVertexPointer(2, GL_FLOAT, 0, v1); glDrawArrays(GL_QUADS, 0, 4);

    /* RIGHT: min=NEAREST, mag=NEAREST -> baseline point sample */
    glGenTextures(1, &tn); glBindTexture(GL_TEXTURE_2D, tn);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TS, TS, 0, GL_RGBA, GL_UNSIGNED_BYTE, checker);
    glVertexPointer(2, GL_FLOAT, 0, v2); glDrawArrays(GL_QUADS, 0, 4);
    glFinish();

    il = scan(144, 324, 21, 2, "LEFT  LINEAR");   /* across the left quad (x 144..184) */
    ir = scan(544, 324, 21, 2, "RIGHT NEAREST");  /* across the right quad */
    printf("VERDICT: %s (LEFT intermediate %d vs RIGHT %d)\n",
           il > ir + 4 ? "min filter WORKS" : "no min-filter effect", il, ir);
    printf("glGetError = 0x%x\n", (unsigned)glGetError());
    fflush(stdout); sleep(8);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
