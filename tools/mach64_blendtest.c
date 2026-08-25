/*
 * mach64_blendtest.c - verify UNTEXTURED alpha blending (glEnable(GL_BLEND) on
 * plain gouraud geometry; previously the untextured pipe was always opaque).
 * Blue background (glClear), then four untextured RED quads left->right at alpha
 * 1.0 / 0.75 / 0.5 / 0.25 with glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
 * PASS = the quads fade from solid red -> reddish -> purple -> mostly-blue as the
 * vertex alpha drops (As blends red over the blue framebuffer).  A last quad uses
 * additive blend (GL_ONE, GL_ONE): green over blue -> cyan.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void quad(float x0, float x1, float y0, float y1)
{
    GLfloat v[8] = { x0,y0, x1,y0, x1,y1, x0,y1 };
    glVertexPointer(2, GL_FLOAT, 0, v);
    glDrawArrays(GL_QUADS, 0, 4);
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx; int i;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL); if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Opaque BLUE background quad (blend off) drawn through our own path, so the
     * dst the blend reads is definitely blue (glClearColor was found not to take). */
    glDisable(GL_BLEND);
    glColor4f(0.f, 0.f, 1.f, 1.f);
    quad(-0.95f, 0.65f, 0.05f, 0.9f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < 4; i++) {                     /* red, alpha 1.0 .. 0.25, over blue */
        float x0 = -0.95f + (float)i * 0.40f, x1 = x0 + 0.34f;
        glColor4f(1.f, 0.f, 0.f, 1.0f - 0.25f * (float)i);
        quad(x0, x1, 0.1f, 0.85f);              /* -> red, red-purple, purple, blue-purple */
    }
    /* additive: green over blue -> cyan (bottom strip) */
    glBlendFunc(GL_ONE, GL_ONE);
    glColor4f(0.f, 1.f, 0.f, 1.f);
    quad(-0.95f, 0.75f, -0.85f, -0.2f);

    glFlush(); glFinish();
    printf("drew blended quads\n"); fflush(stdout);
    sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
