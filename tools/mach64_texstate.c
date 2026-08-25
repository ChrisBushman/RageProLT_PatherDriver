/*
 * mach64_texstate.c - set a specific GL_TEXTURE_2D enable state (selected by
 * argv[1]) then draw, so the gli context can be dumped at sub_4530 and diffed to
 * locate where GLEngine stores the GL_TEXTURE_2D enable bit.  BOTH configs create
 * + upload the SAME texture and bind it -- the ONLY difference is enable/disable,
 * so a gli diff isolates the enable bit.
 *   argv[1]=0 : glEnable(GL_TEXTURE_2D)   (baseline, textured)
 *   argv[1]=1 : glDisable(GL_TEXTURE_2D)  (untextured)
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int cfg = (argc > 1) ? atoi(argv[1]) : 0;
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    GLuint tex = 0;
    static GLubyte px[8*8*3];
    int i;
    AGLContext ctx;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("cfg=%d GL_RENDERER=%s\n", cfg, (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);

    /* Identical texture setup in both configs. */
    for (i = 0; i < 8*8; i++) { px[i*3+0]=255; px[i*3+1]=128; px[i*3+2]=0; }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* The ONLY difference between configs: */
    if (cfg == 1) glDisable(GL_TEXTURE_2D);
    else          glEnable(GL_TEXTURE_2D);

    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    {   static const GLfloat v[6]  = { -0.5f,-0.5f,  0.5f,-0.5f,  0.f,0.5f };
        static const GLfloat tc[6] = {  0.f,0.f,     1.f,0.f,     0.5f,1.f };
        glColor3f(1.f, 1.f, 1.f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, v);
        glTexCoordPointer(2, GL_FLOAT, 0, tc);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFlush(); glFinish();
    printf("drew (cfg=%d)\n", cfg); fflush(stdout);
    sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    return 0;
}
