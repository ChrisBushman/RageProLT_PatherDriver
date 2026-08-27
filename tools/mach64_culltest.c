/* mach64_culltest.c - GL_CULL_FACE (glCullFace/glFrontFace) test + gli-offset scan.
 *
 * Draws TWO triangles, both facing the camera: LEFT wound CCW (a FRONT face under the GL
 * default glFrontFace(GL_CCW)) coloured GREEN, RIGHT wound CW (a BACK face) coloured RED.
 *
 *   Frame 1: GL_CULL_FACE OFF        -> both triangles show (scan baseline).
 *   Frame 2: glCullFace(GL_BACK) + GL_CULL_FACE ON -> the RIGHT (back) triangle vanishes,
 *            the LEFT (front) stays.
 *
 *  - Scan build (driver -DGLD_SCISSCAN): the gli delta pins the GL_CULL_FACE enable word.
 *  - Default build (culling implemented): frame 2 drops the red back triangle;
 *    unimplemented -> both still show.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

/* 2 triangles (6 verts).  LEFT CCW (front), RIGHT CW (back). */
static const GLfloat pos[6*2] = {
    -0.8f,-0.5f,  -0.2f,-0.5f,  -0.5f,0.5f,     /* LEFT  CCW -> front */
     0.2f,-0.5f,   0.5f,0.5f,    0.8f,-0.5f,    /* RIGHT CW  -> back  */
};
static const GLfloat col[6*3] = {
    0,1,0, 0,1,0, 0,1,0,      /* left  green */
    1,0,0, 1,0,0, 1,0,0,      /* right red   */
};

static void draw(void)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glColorPointer(3, GL_FLOAT, 0, col);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);

    /* Frame 1: culling OFF -- both triangles (scan baseline). */
    glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0.15f, 1); glClear(GL_COLOR_BUFFER_BIT);
    draw();
    glFlush(); glFinish();
    sleep(3);

    /* Frame 2: cull GL_BACK -- the RIGHT (back/red) triangle should vanish. */
    glEnable(GL_CULL_FACE);
    glClearColor(0, 0, 0.15f, 1); glClear(GL_COLOR_BUFFER_BIT);
    draw();
    glFlush(); glFinish();
    printf("drew cull OFF then cull GL_BACK.  right(red)=gone/left(green)=stays => culling; "
           "both still shown => not culling\n");
    fflush(stdout);
    sleep(6);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
