/*
 * mach64_texapp.c - INVESTIGATION: drive the texture path so we can (a) see the
 * sequence + args of the gld texture calls GLEngine makes for glTexImage2D, and
 * (b) locate the per-vertex S/T in the 0x100-byte gli vertex (the TEXHUNT dump
 * in render_tri_strip).  Uses DISTINCTIVE texcoords: v0(0.25,0.75) v1(0.5,0.5)
 * v2(0.75,0.25)  -> 0x3e800000 / 0x3f400000 / 0x3f000000.
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
    if (!pf) { printf("no pf\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    {   GLuint tex = 0; GLint mx = -1, mtu = -1;
        static const GLubyte px[64];  /* 4x4 RGBA, zeroed */
        printf("clear err=0x%x\n", glGetError());
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mx);
        printf("GL_MAX_TEXTURE_SIZE=%d (err=0x%x)\n", mx, glGetError());
#ifdef GL_MAX_TEXTURE_UNITS
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, &mtu);
        printf("GL_MAX_TEXTURE_UNITS=%d (err=0x%x)\n", mtu, glGetError());
#endif
        glGenTextures(1, &tex);      printf("genTex(%u) err=0x%x\n", tex, glGetError());
        glBindTexture(GL_TEXTURE_2D, tex);            printf("bind err=0x%x\n", glGetError());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); printf("minf err=0x%x\n", glGetError());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); printf("magf err=0x%x\n", glGetError());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        printf("texImage2D(4x4 RGBA) err=0x%x\n", glGetError());
        glTexImage2D(GL_TEXTURE_2D, 0, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        printf("texImage2D(intfmt=4) err=0x%x\n", glGetError());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
        printf("texImage2D(8x8 RGB) err=0x%x\n", glGetError());
        glEnable(GL_TEXTURE_2D);     printf("enable err=0x%x\n", glGetError());
    }

    {   static const GLfloat verts[6] = { -0.5f,-0.5f,  0.5f,-0.5f,  0.f,0.5f };
        static const GLfloat tc[6]    = {  0.25f,0.75f, 0.5f,0.5f,   0.75f,0.25f };
        glColor3f(1.f, 1.f, 1.f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glTexCoordPointer(2, GL_FLOAT, 0, tc);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFlush(); glFinish();
    printf("drew textured triangle (GL err=0x%x)\n", glGetError()); fflush(stdout);
    sleep(3);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
