/*
 * mach64_texalpha.c - prove a real RGBA texel alpha reaches the pipeline
 * (TEX_MAP_AEN) using GL_DECAL, which blends the texture over the fragment colour
 * by texel alpha ENTIRELY in the texture unit (no framebuffer blend needed):
 *     Cout = Cfrag*(1-At) + Ctex*At
 *
 * Texture: 8x8 RGBA, solid RED (255,0,0), alpha ramped 0..255 left->right.
 * Vertex (fragment) colour: solid BLUE.
 *   Q0 GL_DECAL  -> left At=0 shows BLUE, right At=255 shows RED, a blue->red
 *                   ramp across 8 columns.  THIS proves texel alpha works.
 *   Q1 GL_REPLACE-> Cout=Ctex=red everywhere; alpha is not visible without a
 *                   framebuffer blend (that's increment 2), so a solid RED quad
 *                   is the expected control.
 *
 * PASS: Q0 a blue->red horizontal ramp, Q1 solid red.
 * FAIL: Q0 solid red (texel alpha ignored -> TEX_MAP_AEN not taking effect).
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
        d[0]=255; d[1]=0; d[2]=0; d[3]=(GLubyte)(x*255/7);   /* red, alpha ramp */
    }
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return t;
}

static void quad(GLuint t, GLint mode, int slot)
{
    float x0 = -0.9f + (float)slot * 0.95f, x1 = x0 + 0.8f;
    GLfloat v[12] = { x0,-0.6f, x1,-0.6f, x1,0.6f,  x0,-0.6f, x1,0.6f, x0,0.6f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
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
    glColor3f(0.f, 0.f, 1.f);          /* blue fragment colour */
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("Q0 DECAL -> blue->red ramp (texel alpha); Q1 REPLACE -> solid red\n");
    quad(t, GL_DECAL,   0);
    quad(t, GL_REPLACE, 1);
    printf("drew\n"); fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
