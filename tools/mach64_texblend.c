/*
 * mach64_texblend.c - framebuffer alpha blending: an RGBA texture's texel alpha
 * blends the textured quad against the FRAMEBUFFER (not the vertex colour) via
 * glEnable(GL_BLEND) + glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
 *
 * Background cleared to BLUE.  Texture: 8x8 RGBA, solid RED, alpha 0..255 L->R,
 * env GL_REPLACE (fragment colour = texel, As = texel alpha).
 *   Q0 BLEND ON  -> Cout = red*As + blue*(1-As): a blue->red ramp over the bg.
 *   Q1 BLEND OFF -> opaque red (alpha ignored).
 *
 * PASS: Q0 blue(left)->red(right) ramp, Q1 solid red.
 * FAIL: Q0 solid red (blend enable/func not honored) or unchanged bg (all blue).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLuint alpha_tex(void)
{
    static GLubyte px[8*8*4];
    int x, y; GLuint t = 0;
    for (y=0;y<8;y++) for (x=0;x<8;x++){
        GLubyte *d = &px[(y*8+x)*4];
        d[0]=255; d[1]=0; d[2]=0; d[3]=(GLubyte)(x*255/7);
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return t;
}

static void quad(GLuint t, int blend, int slot)
{
    float x0 = -0.9f + (float)slot * 0.95f, x1 = x0 + 0.8f;
    GLfloat v[12] = { x0,-0.6f, x1,-0.6f, x1,0.6f,  x0,-0.6f, x1,0.6f, x0,0.6f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else       glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, t);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFlush(); glFinish();
}

int main(void)
{
    GDHandle dev = GetMainDevice();
    GLint attribs[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN,
                        AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev, 1, attribs);
    AGLContext ctx;
    GLuint t;
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, 1024, 768);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    t = alpha_tex();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Opaque BLUE background quad (untextured) -> a reliable blend destination,
     * not relying on the clear colour. */
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.f, 0.f, 1.f);
    {   GLfloat bv[12] = { -1,-1, 1,-1, 1,1,  -1,-1, 1,1, -1,1 };
        glVertexPointer(2, GL_FLOAT, 0, bv);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glFlush(); glFinish();
    }
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    printf("Q0 BLEND -> blue->red ramp; Q1 no-blend -> solid red\n");
    quad(t, 1, 0);
    quad(t, 0, 1);
    printf("drew\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
