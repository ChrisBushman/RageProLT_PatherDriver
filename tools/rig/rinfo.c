#include <OpenGL/OpenGL.h>
#include <stdio.h>
int main(void){
  CGLRendererInfoObj ri; long n=0,i,v;
  CGLError e=CGLQueryRendererInfo(0xFFFFFFFFu,&ri,&n);
  printf("err=%d n=%ld\n",(int)e,n);
  for(i=0;i<n;i++){
    CGLDescribeRenderer(ri,i,kCGLRPRendererID,&v); printf("[%ld] id=0x%lx",i,v);
    CGLDescribeRenderer(ri,i,kCGLRPAccelerated,&v); printf(" accel=%ld",v);
    CGLDescribeRenderer(ri,i,kCGLRPOffScreen,&v); printf(" offscreen=%ld",v);
    CGLDescribeRenderer(ri,i,kCGLRPFullScreen,&v); printf(" fullscreen=%ld",v);
    CGLDescribeRenderer(ri,i,kCGLRPDisplayMask,&v); printf(" dmask=0x%lx\n",v);
  }
  return 0;}
