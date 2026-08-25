/*
 * mach64_ptline.c - verify the GL_POINTS / GL_LINES / GL_LINE_STRIP / GL_LINE_LOOP
 * render paths (each expands to triangle-path quads; dispatch slots 0x18/0x1c/0x20
 * recovered by the GLD_DISPATCH_PROBE run).  Untextured, per-vertex colour, so the
 * shape + colour verify the quad expansion, point size (glPointSize @gli 0x6630),
 * line width (glLineWidth @gli 0x6394) and gouraud interpolation along lines.
 * Four horizontal bands, top to bottom on screen:
 *   band 0  GL_POINTS      5 fat points, distinct colours
 *   band 1  GL_LINES       3 separate segments, each endpoint a different colour
 *   band 2  GL_LINE_STRIP  zigzag polyline
 *   band 3  GL_LINE_LOOP   closed square outline
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0.f, 0.f, 0.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);

    /* band 0: GL_POINTS -- 5 fat points across the top */
    glPointSize(14.f);
    glBegin(GL_POINTS);
      glColor3f(1,0,0); glVertex2f(-0.8f, 0.75f);
      glColor3f(0,1,0); glVertex2f(-0.4f, 0.75f);
      glColor3f(0,0,1); glVertex2f( 0.0f, 0.75f);
      glColor3f(1,1,0); glVertex2f( 0.4f, 0.75f);
      glColor3f(1,1,1); glVertex2f( 0.8f, 0.75f);
    glEnd();

    /* band 1: GL_LINES -- 3 separate segments, endpoints differ (gouraud) */
    glLineWidth(3.f);
    glBegin(GL_LINES);
      glColor3f(1,0,0); glVertex2f(-0.9f, 0.30f); glColor3f(0,1,0); glVertex2f(-0.5f, 0.30f);
      glColor3f(0,1,0); glVertex2f(-0.2f, 0.30f); glColor3f(0,0,1); glVertex2f( 0.2f, 0.30f);
      glColor3f(0,0,1); glVertex2f( 0.5f, 0.30f); glColor3f(1,0,0); glVertex2f( 0.9f, 0.30f);
    glEnd();

    /* band 2: GL_LINE_STRIP -- zigzag polyline */
    glBegin(GL_LINE_STRIP);
      glColor3f(1,0,0); glVertex2f(-0.9f, -0.15f);
      glColor3f(0,1,0); glVertex2f(-0.5f,  0.05f);
      glColor3f(0,0,1); glVertex2f(-0.1f, -0.15f);
      glColor3f(1,1,0); glVertex2f( 0.3f,  0.05f);
      glColor3f(1,1,1); glVertex2f( 0.7f, -0.15f);
    glEnd();

    /* band 3: GL_LINE_LOOP -- closed square outline */
    glBegin(GL_LINE_LOOP);
      glColor3f(1,0,0); glVertex2f(-0.3f, -0.45f);
      glColor3f(0,1,0); glVertex2f( 0.3f, -0.45f);
      glColor3f(0,0,1); glVertex2f( 0.3f, -0.85f);
      glColor3f(1,1,1); glVertex2f(-0.3f, -0.85f);
    glEnd();

    glFlush(); glFinish();
    printf("drew points+lines\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
