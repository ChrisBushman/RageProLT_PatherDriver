/* mach64_fogscan.c - recover the GL_FOG-enable gli offset.  Draws the SAME scene
 * twice, changing ONLY fog between the two frames:
 *   frame 1: GL_FOG disabled  (driver silently snapshots the gli state cluster)
 *   frame 2: GL_FOG enabled    (driver prints the FOGSCAN delta -> the enable word,
 *                               the fog colour at +0x6358, and any mode/start/end)
 * The per-vertex +0x4c fog-factor dump (also FOGSCAN) confirms it varies with depth.
 * Build the DRIVER with -DGLD_FOGSCAN; build this tool:
 *   cc -o mach64_fogscan mach64_fogscan.c -framework Carbon -framework AGL -framework OpenGL
 * Run under sudo; then read the driver log (not a picture -- this is a text scan). */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static void scene(void)
{
    float zs[4] = { -0.1f, -0.4f, -0.7f, -0.95f };
    int i;
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 0, 0);
    for (i = 0; i < 4; i++) {
        float x0 = -0.95f + (float)i * 0.48f, x1 = x0 + 0.40f, z = zs[i];
        GLfloat v[12] = { x0,-0.5f,z,  x1,-0.5f,z,  x1,0.5f,z,  x0,0.5f,z };
        glVertexPointer(3, GL_FLOAT, 0, v);
        glDrawArrays(GL_QUADS, 0, 4);
    }
    glFlush();
    glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint at[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, at);
    AGLContext ctx;
    GLfloat fc[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   /* WHITE fog: distinct in the delta */

    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);

    /* Configure fog params up front (harmless while disabled), so the ONLY thing
     * that changes between the two frames is the GL_FOG enable itself. */
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fc);
    glFogf(GL_FOG_START, 0.0f);
    glFogf(GL_FOG_END, 1.0f);

    glDisable(GL_FOG);
    printf("frame 1: GL_FOG OFF (baseline snapshot)\n"); fflush(stdout);
    scene();

    glEnable(GL_FOG);
    printf("frame 2: GL_FOG ON (delta = enable word + fog colour)\n"); fflush(stdout);
    scene();

    printf("done -- read the driver log for FOGSCAN lines\n"); fflush(stdout);
    sleep(3);
    aglSetCurrentContext(NULL);
    aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx);
    aglDestroyPixelFormat(pf);
    return 0;
}
