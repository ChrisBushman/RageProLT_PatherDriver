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
        /* DIAGNOSTIC 8x8 RGB texture: four solid quadrants so the screenshot
         * reveals colour-channel order AND orientation at a glance --
         *   top-left RED  top-right GREEN  bottom-left BLUE  bottom-right WHITE
         * (texel row 0 = T=0).  If channels are swapped we'll see it; if the
         * image is flipped/rotated the quadrant layout shows how. */
        /* Four-quadrant 8x8: top-left RED, top-right GREEN, bottom-left BLUE,
         * bottom-right WHITE (row 0 = T=0).  Validates per-texel upload AND S/T
         * addressing/orientation at a glance. */
        static GLubyte px[8*8*3];
        int x, y;
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
            GLubyte *p = &px[(y*8 + x)*3];
            int top = (y < 4), left = (x < 4);
            if      ( top &&  left) { p[0]=255; p[1]=0;   p[2]=0;   }   /* red   */
            else if ( top && !left) { p[0]=0;   p[1]=255; p[2]=0;   }   /* green */
            else if (!top &&  left) { p[0]=0;   p[1]=0;   p[2]=255; }   /* blue  */
            else                    { p[0]=255; p[1]=255; p[2]=255; }   /* white */
        }
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
        printf("texImage2D(8x8 RGB quadrants) err=0x%x\n", glGetError());
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnable(GL_TEXTURE_2D);     printf("enable err=0x%x\n", glGetError());
    }

    glColor3f(1.f, 1.f, 1.f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    /* LEFT quad: GL_TEXTURE_2D enabled -> the four colour quadrants. */
    {   static const GLfloat verts[12] = { -0.9f,-0.6f, -0.1f,-0.6f, -0.1f,0.6f,
                                           -0.9f,-0.6f, -0.1f, 0.6f, -0.9f,0.6f };
        static const GLfloat tc[12]    = {  0.f,0.f,  1.f,0.f,  1.f,1.f,
                                            0.f,0.f,  1.f,1.f,  0.f,1.f };
        glEnable(GL_TEXTURE_2D);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glTexCoordPointer(2, GL_FLOAT, 0, tc);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    /* RIGHT quad: GL_TEXTURE_2D DISABLED -> should be solid white (vertex colour)
     * if glDisable(GL_TEXTURE_2D) is honoured (else it would be textured too). */
    {   static const GLfloat verts[12] = {  0.1f,-0.6f,  0.9f,-0.6f,  0.9f,0.6f,
                                            0.1f,-0.6f,  0.9f, 0.6f,  0.1f,0.6f };
        static const GLfloat tc[12]    = {  0.f,0.f,  1.f,0.f,  1.f,1.f,
                                            0.f,0.f,  1.f,1.f,  0.f,1.f };
        glDisable(GL_TEXTURE_2D);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glTexCoordPointer(2, GL_FLOAT, 0, tc);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glFlush(); glFinish();
    printf("drew textured(left)+untextured(right) quads (GL err=0x%x)\n", glGetError()); fflush(stdout);
    sleep(4);

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf);
    printf("done\n");
    return 0;
}
