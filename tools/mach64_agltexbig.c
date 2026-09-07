/*
 * mach64_agltexbig.c - LARGE-window textured + depth test for Path B.
 *
 * An 800x600 window (far larger than the 320x240 small-window cases) whose colour surface + Z buffer
 * + texture heap CANNOT fit in the safe VRAM gap below Apple's desktop surfaces -- so it exercises
 * the general Apple-VRAM allocator (kext selector), not the small-window guessed placement.  Draws
 * two depth-tested textured quads (a near checkerboard partly occluding a far one) so it stresses
 * textures AND depth AND a large surface at once -- the fullscreen-class workload, minus fullscreen's
 * CRTC/coexistence risk.  MUST run AT THE CONSOLE.
 * Build: cc -O2 -o ~/agltexbig mach64_agltexbig.c -framework AGL -framework OpenGL -framework Carbon
 */
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    Rect bounds = { 40, 40, 640, 840 };              /* 800x600 */
    WindowRef win = NULL;
    OSStatus err = CreateNewWindow(kDocumentWindowClass,
                                   kWindowStandardDocumentAttributes, &bounds, &win);
    if (err || !win) { printf("CreateNewWindow err=%d\n", (int)err); return 1; }
    SetWindowTitleWithCFString(win, CFSTR("mach64 Path-B texture+depth (800x600)"));
    ShowWindow(win);

    GLint attrs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_ACCELERATED, AGL_DEPTH_SIZE, 16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(NULL, 0, attrs);
    printf("aglChoosePixelFormat -> %p (err %d)\n", (void*)pf, (int)aglGetError());
    if (!pf) { printf("no ACCELERATED depth pixel format\n"); return 1; }
    AGLContext ctx = aglCreateContext(pf, NULL);
    if (!ctx) { printf("aglCreateContext failed (err %d)\n", (int)aglGetError()); return 1; }
    aglSetDrawable(ctx, GetWindowPort(win));
    aglSetCurrentContext(ctx);

    /* 128x128 red/yellow checkerboard */
    static unsigned char tex[128*128*4];
    int x, y;
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) {
        int c = ((x >> 4) ^ (y >> 4)) & 1; unsigned char *p = &tex[(y*128+x)*4];
        p[0] = 255; p[1] = c ? 255 : 0; p[2] = 0; p[3] = 255;
    }
    GLuint id = 0;
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    printf("texture uploaded (glGetError=0x%x)\n", (int)glGetError());

    glViewport(0, 0, 800, 600);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);

    printf("rendering 180 frames (800x600 textured + depth)...\n"); fflush(stdout);
    int i;
    for (i = 0; i < 180; i++) {
        glClearColor(0.10f, 0.10f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* FAR textured quad (eye z=-0.6 -> far), lower-left */
        glBegin(GL_TRIANGLES);
            glTexCoord2f(0,0); glVertex3f(-0.9f,-0.9f,-0.6f);
            glTexCoord2f(1,0); glVertex3f( 0.5f,-0.9f,-0.6f);
            glTexCoord2f(1,1); glVertex3f( 0.5f, 0.5f,-0.6f);
            glTexCoord2f(0,0); glVertex3f(-0.9f,-0.9f,-0.6f);
            glTexCoord2f(1,1); glVertex3f( 0.5f, 0.5f,-0.6f);
            glTexCoord2f(0,1); glVertex3f(-0.9f, 0.5f,-0.6f);
        glEnd();
        /* NEAR textured quad (eye z=+0.6 -> near), upper-right, overlapping */
        glBegin(GL_TRIANGLES);
            glTexCoord2f(0,0); glVertex3f(-0.5f,-0.5f, 0.6f);
            glTexCoord2f(2,0); glVertex3f( 0.9f,-0.5f, 0.6f);
            glTexCoord2f(2,2); glVertex3f( 0.9f, 0.9f, 0.6f);
            glTexCoord2f(0,0); glVertex3f(-0.5f,-0.5f, 0.6f);
            glTexCoord2f(2,2); glVertex3f( 0.9f, 0.9f, 0.6f);
            glTexCoord2f(0,2); glVertex3f(-0.5f, 0.9f, 0.6f);
        glEnd();

        aglSwapBuffers(ctx);
        usleep(16000);
    }
    printf("done -- 180 frames presented\n");

    aglSetCurrentContext(NULL); aglSetDrawable(ctx, NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf); DisposeWindow(win);
    return 0;
}
