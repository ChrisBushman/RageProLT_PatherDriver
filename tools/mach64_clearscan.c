/* mach64_clearscan.c - set a distinctive glClearColor(0.6,0.3,0.9) and draw one
 * untextured quad so the -DGLD_CLEARSCAN driver scans the gli for the clear-colour
 * offset.  Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>
int main(void){
    GDHandle dev = GetMainDevice();
    GLint a[] = { AGL_RGBA, AGL_ACCELERATED, AGL_FULLSCREEN, AGL_DEPTH_SIZE,16, AGL_NONE };
    AGLPixelFormat pf = aglChoosePixelFormat(&dev,1,a); AGLContext ctx;
    GLfloat v[8]={-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f};
    if(!pf){printf("no pf\n");return 1;} ctx=aglCreateContext(pf,NULL); if(!ctx)return 1;
    aglSetFullScreen(ctx,0,0,0,0); aglSetCurrentContext(ctx);
    glViewport(0,0,1024,768); glDisable(GL_TEXTURE_2D);
    glClearColor(0.6f,0.3f,0.9f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY); glColor4f(1,1,1,1);
    glVertexPointer(2,GL_FLOAT,0,v); glDrawArrays(GL_QUADS,0,4);
    glFlush(); glFinish(); printf("drew\n"); sleep(2);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx,NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf); return 0;
}
