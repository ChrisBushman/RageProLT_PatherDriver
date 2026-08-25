/* mach64_colormask.c - glColorMask.  A WHITE untextured quad on black with
 * glColorMask(GL_TRUE,GL_TRUE,GL_FALSE,GL_TRUE) (blue disabled): once implemented
 * the quad renders YELLOW (R+G, no B).  -DGLD_CMSCAN build scans for the gli offset.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>
int main(void){
    GDHandle dev=GetMainDevice();
    GLint at[]={AGL_RGBA,AGL_ACCELERATED,AGL_FULLSCREEN,AGL_DEPTH_SIZE,16,AGL_NONE};
    AGLPixelFormat pf=aglChoosePixelFormat(&dev,1,at); AGLContext ctx;
    GLfloat v[8]={-0.6f,-0.6f, 0.6f,-0.6f, 0.6f,0.6f, -0.6f,0.6f};
    if(!pf){printf("no pf\n");return 1;} ctx=aglCreateContext(pf,NULL); if(!ctx)return 1;
    aglSetFullScreen(ctx,0,0,0,0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n",(const char*)glGetString(GL_RENDERER));
    glViewport(0,0,1024,768); glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE,GL_TRUE,GL_FALSE,GL_TRUE);   /* disable BLUE */
    glColor4f(1,1,1,1);                              /* white -> should be yellow */
    glVertexPointer(2,GL_FLOAT,0,v); glDrawArrays(GL_QUADS,0,4);
    glFlush(); glFinish(); printf("drew\n"); fflush(stdout); sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx,NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf); return 0;
}
