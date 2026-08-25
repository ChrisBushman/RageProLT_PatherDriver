/* mach64_alphatest.c - GL_ALPHA_TEST.  An RGBA texture with a horizontal alpha
 * ramp (left alpha=0 -> right alpha=1) on a quad, glAlphaFunc(GL_GREATER, 0.5):
 * texels with alpha <= 0.5 should be DISCARDED (show the coloured background),
 * texels with alpha > 0.5 kept.  So the left half of the quad drops out.
 * The -DGLD_ATSCAN build (with ref 0.6 here) scans the gli for the state offsets.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>
static GLuint ramp_tex(void){
    static GLubyte px[8*8*4]; int x,y; GLuint t=0;
    for(y=0;y<8;y++)for(x=0;x<8;x++){ GLubyte*d=&px[(y*8+x)*4];
        d[0]=255; d[1]=255; d[2]=0; d[3]=(GLubyte)(x*255/7); }   /* yellow, alpha ramp */
    glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,px); return t;
}
int main(void){
    GDHandle dev=GetMainDevice();
    GLint at[]={AGL_RGBA,AGL_ACCELERATED,AGL_FULLSCREEN,AGL_DEPTH_SIZE,16,AGL_NONE};
    AGLPixelFormat pf=aglChoosePixelFormat(&dev,1,at); AGLContext ctx;
    GLfloat v[8]={-0.7f,-0.6f, 0.7f,-0.6f, 0.7f,0.6f, -0.7f,0.6f};
    GLfloat tc[8]={0,0, 1,0, 1,1, 0,1};
    if(!pf){printf("no pf\n");return 1;} ctx=aglCreateContext(pf,NULL); if(!ctx)return 1;
    aglSetFullScreen(ctx,0,0,0,0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n",(const char*)glGetString(GL_RENDERER));
    glViewport(0,0,1024,768); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    ramp_tex(); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE); glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.f,0.f,0.5f,1.f); glClear(GL_COLOR_BUFFER_BIT);   /* dark-blue bg */
    glAlphaFunc(GL_GREATER, 0.6f); glEnable(GL_ALPHA_TEST);
    glVertexPointer(2,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,tc);
    glDrawArrays(GL_QUADS,0,4);
    glFlush(); glFinish(); printf("drew\n"); fflush(stdout); sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx,NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf); return 0;
}
