/* mach64_clipptlntest.c - probe/verify clipping of POINTS and LINES.
 *
 * Companion to the triangle scissor work (docs/re-notes/scissor.md): does GLEngine also
 * software-clip points/lines and re-dispatch them through the element vectors 0x40
 * (ELT_POINTS) / 0x44 (ELT_LINES) as vertex-pointer arrays, or through the flat slots
 * 0x18/0x1c?  The driver logs FLAT_POINTS/FLAT_LINES vs ELT_POINTS/ELT_LINES + the format.
 *
 * Uses glDrawArrays (NOT immediate mode: glBegin fullscreen gave gldAttachDrawable a bogus
 * drawable descriptor -> garbage ctx+0x1f4 height -> everything rendered offscreen).
 *
 * Scissor box glScissor(300,250,400,300) -> GL x[300,700], y[250,550] (bottom-left origin);
 * on the FB (top-left) that is x[300,700], y[218,518] -- a centred box.
 *   POINTS: a horizontal row at GL y=400 (inside the box vertically) spanning x=40..980, so
 *           only x in [300,700] survive the scissor.
 *   LINES: segments crossing the box edges (clipped to the boundary):
 *     A (40,400)->(500,400)   crosses LEFT   x=300
 *     B (500,400)->(980,400)  crosses RIGHT  x=700
 *     C (500,120)->(500,680)  crosses TOP+BOTTOM y=250/550 (GL)
 *     D (300,250)->(700,550)  box diagonal (fully inside, corner to corner)
 *
 * Frame 1 scissor OFF (baseline: full row + full lines); frame 2 scissor ON (clipped).
 * PASS(frame 2): points only within the box's x-span; lines trimmed to the box edges.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

#define NDCX(wx) ((wx) / 512.0f - 1.0f)     /* window (y-up) -> NDC, 1024x768 viewport */
#define NDCY(wy) ((wy) / 384.0f - 1.0f)

static GLfloat pts[24 * 2];                  /* row of 24 points at y=400 */
static GLfloat lns[8 * 2] = {                /* 4 lines = 8 endpoints */
    NDCX(40),NDCY(400),  NDCX(500),NDCY(400),    /* A crosses left  */
    NDCX(500),NDCY(400), NDCX(980),NDCY(400),    /* B crosses right */
    NDCX(500),NDCY(120), NDCX(500),NDCY(680),    /* C crosses top+bottom */
    NDCX(300),NDCY(250), NDCX(700),NDCY(550),    /* D box diagonal */
};

static void draw_scene(void)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glPointSize(9.0f);
    glColor3f(1, 1, 0);                       /* yellow points */
    glVertexPointer(2, GL_FLOAT, 0, pts);
    glDrawArrays(GL_POINTS, 0, 24);

    glLineWidth(3.0f);
    glColor3f(1, 1, 1);                       /* white lines */
    glVertexPointer(2, GL_FLOAT, 0, lns);
    glDrawArrays(GL_LINES, 0, 8);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    int i;

    for (i = 0; i < 24; i++) { pts[i*2] = NDCX(40.0f + i*40.0f); pts[i*2+1] = NDCY(400.0f); }

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);

    /* Frame 1: scissor OFF -- full row + full lines. */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT);
    draw_scene();
    glFlush(); glFinish();
    sleep(3);

    /* Frame 2: scissor ON -- points/lines clipped to the box. */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glScissor(300, 250, 400, 300);
    glEnable(GL_SCISSOR_TEST);
    draw_scene();
    glFlush(); glFinish();
    printf("drew points+lines, scissor OFF then ON glScissor(300,250,400,300)\n");
    fflush(stdout);
    sleep(6);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
