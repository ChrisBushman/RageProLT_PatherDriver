/* mach64_fogtest.c - is GL_FOG applied?  Four RED quads at increasing eye-distance
 * (z = -0.1..-0.95) with LINEAR WHITE fog (start 0, end 1).  If fog works they fade
 * red -> pink -> white left->right.  Run against the CURRENT driver first: if
 * GLEngine software-fogs the vertex colours it already works; if not (all red), the
 * hardware fog path (ALPHA_FOG_EN_FOG + DP_FOG_CLR + vertex-alpha factor) is needed.
 * Build: cc -framework Carbon -framework AGL -framework OpenGL */
#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <unistd.h>
int main(void){
    GDHandle dev=GetMainDevice();
    GLint at[]={AGL_RGBA,AGL_ACCELERATED,AGL_FULLSCREEN,AGL_DEPTH_SIZE,16,AGL_NONE};
    AGLPixelFormat pf=aglChoosePixelFormat(&dev,1,at); AGLContext ctx; int i;
    GLfloat fc[4]={0.2f,0.5f,0.8f,1}; float zs[4]={-0.1f,-0.4f,-0.7f,-0.95f};
    if(!pf){printf("no pf\n");return 1;} ctx=aglCreateContext(pf,NULL); if(!ctx)return 1;
    aglSetFullScreen(ctx,0,0,0,0); aglSetCurrentContext(ctx);
    printf("GL_RENDERER=%s\n",(const char*)glGetString(GL_RENDERER));
    glViewport(0,0,1024,768); glDisable(GL_TEXTURE_2D);
    glEnable(GL_FOG); glFogi(GL_FOG_MODE,GL_LINEAR);
    glFogfv(GL_FOG_COLOR,fc); glFogf(GL_FOG_START,0.0f); glFogf(GL_FOG_END,1.0f);
    glEnableClientState(GL_VERTEX_ARRAY); glColor3f(1,0,0);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    for(i=0;i<4;i++){
        float x0=-0.95f+(float)i*0.48f, x1=x0+0.40f, y0=-0.5f, y1=0.5f, z=zs[i];
        GLfloat v[12]={x0,y0,z, x1,y0,z, x1,y1,z, x0,y1,z};
        glVertexPointer(3,GL_FLOAT,0,v); glDrawArrays(GL_QUADS,0,4);
    }
    glFlush(); glFinish(); printf("drew fog quads\n"); fflush(stdout); sleep(6);
    aglSetCurrentContext(NULL); aglSetDrawable(ctx,NULL);
    aglDestroyContext(ctx); aglDestroyPixelFormat(pf); return 0;
}
