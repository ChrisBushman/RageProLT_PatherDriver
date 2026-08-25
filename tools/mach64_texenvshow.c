/*
 * mach64_texenvshow.c - visually confirm the driver honors GL_TEXTURE_ENV_MODE.
 * Three quads, SAME orange texture (255,128,0), SAME non-white vertex colour
 * (cyan 0,255,255), differing ONLY in env mode:
 *
 *   Q0 GL_MODULATE : orange * cyan = (0,128,0)  -> GREEN   (colour modulates)
 *   Q1 GL_REPLACE  : orange, colour ignored     -> ORANGE
 *   Q2 GL_DECAL    : orange (RGB decal == replace, texel alpha 0xff) -> ORANGE
 *
 * PASS: Q0 green, Q1 and Q2 both orange (identical). That proves REPLACE/DECAL
 * drop the vertex colour while MODULATE multiplies by it -> env mode is honored.
 * (REPLACE vs DECAL can only differ for an RGBA texture with real alpha, which
 * also needs TEX_MAP_AEN -- future work; for RGB they are identical by spec.)
 * FAIL: all three green (env mode ignored, still hardcoded MODULATE) or all
 * three orange (colour never reaches the modulator).
 * Build: cc -framework Carbon -framework AGL -framework OpenGL
 */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>

static GLuint make_tex(GLubyte r, GLubyte g, GLubyte b)
{
    static GLubyte px[8*8*3];
    GLuint t = 0; int i;
    for (i = 0; i < 8*8; i++) { px[i*3]=r; px[i*3+1]=g; px[i*3+2]=b; }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    return t;
}

/* Draw the bound texture at screen slot `slot` under env mode `mode`. */
static void quad(GLint mode, int slot)
{
    float x0 = -0.9f + (float)slot * 0.62f, x1 = x0 + 0.5f;
    GLfloat v[12] = { x0,-0.5f, x1,-0.5f, x1,0.5f,  x0,-0.5f, x1,0.5f, x0,0.5f };
    GLfloat tc[12]= { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
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
    if (!pf) { printf("no pf\n"); return 1; }
    ctx = aglCreateContext(pf, NULL);
    if (!ctx) return 1;
    aglSetFullScreen(ctx, 0, 0, 0, 0);
    aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n", (const char *)glGetString(GL_RENDERER));

    glViewport(0, 0, 1024, 768);
    make_tex(255, 128, 0);              /* orange, left bound */
    glColor3f(0.f, 1.f, 1.f);           /* cyan vertex colour */
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    printf("Q0 MODULATE -> expect GREEN (orange*cyan)\n");  quad(GL_MODULATE, 0);
    printf("Q1 REPLACE  -> expect ORANGE\n");               quad(GL_REPLACE,  1);
    printf("Q2 DECAL    -> expect ORANGE (==replace for RGB)\n"); quad(GL_DECAL, 2);

    printf("drew MODULATE(green) / REPLACE(orange) / DECAL(orange)\n");
    fflush(stdout);
    sleep(5);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
