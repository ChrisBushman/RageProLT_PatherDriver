/* mach64_shadetest.c - glShadeModel(GL_FLAT vs GL_SMOOTH) test + gli-offset scan.
 *
 * Draws one big triangle with per-vertex colours red / green / blue.  Frame 1 GL_SMOOTH
 * (a smooth RGB gradient across the triangle), frame 2 GL_FLAT (the WHOLE triangle takes
 * the provoking = last vertex's colour -- BLUE here, since vertex 2 is blue).
 *
 *  - Scan build (driver -DGLD_SCISSCAN): the gli delta between the two frames pins the
 *    glShadeModel state word.
 *  - Default build (flat shading implemented): frame 2 is a SOLID blue triangle;
 *    unimplemented -> frame 2 still shows the gradient.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

/* big triangle, per-vertex RGB (vertex 2 = blue = the GL provoking vertex) */
static const GLfloat pos[3*2] = { -0.8f,-0.7f,   0.8f,-0.7f,   0.0f,0.8f };
static const GLfloat col[3*3] = {  1,0,0,        0,1,0,        0,0,1 };

static void draw(void)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, pos);
    glColorPointer(3, GL_FLOAT, 0, col);
    glDrawArrays(GL_TRIANGLES, 0, 3);
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

    /* Frame 1: GL_SMOOTH -- gradient (scan baseline). */
    glShadeModel(GL_SMOOTH);
    glClearColor(0, 0, 0.15f, 1); glClear(GL_COLOR_BUFFER_BIT);
    draw();
    glFlush(); glFinish();
    sleep(3);

    /* Frame 2: GL_FLAT -- whole triangle = provoking (last) vertex colour = BLUE. */
    glShadeModel(GL_FLAT);
    glClearColor(0, 0, 0.15f, 1); glClear(GL_COLOR_BUFFER_BIT);
    draw();
    glFlush(); glFinish();
    printf("drew GL_SMOOTH then GL_FLAT.  flat => solid blue triangle; still-gradient => "
           "flat shading not applied\n");
    fflush(stdout);
    sleep(6);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
