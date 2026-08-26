/* mach64_dmastress.c - stress the DMA batch-split fix (12-bit byte-count limit).
 *
 * Draws a dense 40x30 grid of gouraud quads (2400 triangles, 7200 vertices) filling the
 * screen with a smooth colour gradient: R = x fraction (left->right), G = y fraction
 * (bottom->top), B = 0.3.  At ~33 triangles per 3840-byte submit that is ~73 DMA submits
 * for a single glDrawArrays -- far past the old ~1KB truncation point.
 *
 * PASS: the ENTIRE screen shows one smooth gradient (dark bottom-left -> red bottom-right ->
 *       green top-left -> yellow top-right), no missing rectangles, no colour banding.
 *   - a missing region  => a submit was still truncated (byte-count cap too high).
 *   - a horizontal band of wrong/flat colour => a post-split batch lost its render state
 *     (state not re-emitted after a submit).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#define COLS 40
#define ROWS 30
#define NVERT (COLS * ROWS * 6)          /* 6 verts (2 tris) per cell */

static GLfloat pos[NVERT * 2];
static GLfloat col[NVERT * 3];

static void put(int *k, float x, float y) {   /* x,y in [0,1] screen fraction */
    pos[*k*2+0] = x * 2.0f - 1.0f;            /* -> NDC */
    pos[*k*2+1] = y * 2.0f - 1.0f;
    col[*k*3+0] = x;                          /* R = x, G = y, B = 0.3 */
    col[*k*3+1] = y;
    col[*k*3+2] = 0.3f;
    (*k)++;
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    int i, j, k = 0;

    for (j = 0; j < ROWS; j++) for (i = 0; i < COLS; i++) {
        float x0 = (float)i / COLS, x1 = (float)(i+1) / COLS;
        float y0 = (float)j / ROWS, y1 = (float)(j+1) / ROWS;
        put(&k, x0,y0); put(&k, x1,y0); put(&k, x1,y1);   /* tri 1 */
        put(&k, x0,y0); put(&k, x1,y1); put(&k, x0,y1);   /* tri 2 */
    }

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s  verts=%d tris=%d\n",
           (const char *)glGetString(GL_RENDERER), NVERT, NVERT/3);
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0, 0, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glColorPointer(3, GL_FLOAT, 0, col);
    glDrawArrays(GL_TRIANGLES, 0, NVERT);
    glFlush(); glFinish();
    printf("drew %d triangles across many DMA submits\n", NVERT/3);
    fflush(stdout);
    sleep(8);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
