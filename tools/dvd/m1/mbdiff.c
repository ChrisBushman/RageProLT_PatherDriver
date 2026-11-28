/* mbdiff a.yuv b.yuv fa fb : list 16x16 luma MBs (frame coords) whose max abs diff > 2 between frame fa of a and frame fb of b (720x480 I420) */
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long fs=W*H*3/2;FILE*a=fopen(v[1],"rb"),*b=fopen(v[2],"rb");int fa=atoi(v[3]),fb=atoi(v[4]);
unsigned char*A=malloc(fs),*B=malloc(fs);fseek(a,fa*fs,0);fseek(b,fb*fs,0);fread(A,1,fs,a);fread(B,1,fs,b);int n=0;
for(int my=0;my<H/16;my++)for(int mx=0;mx<W/16;mx++){int md=0,mt=0,mbt=0;for(int y=0;y<16;y++)for(int x=0;x<16;x++){int i=(my*16+y)*W+mx*16+x;int d=abs(A[i]-B[i]);if(d>md)md=d;if((y&1)==0&&d>2)mt=1;if((y&1)&&d>2)mbt=1;}
 if(md>2){n++;printf("MB(%d,%d) maxdiff %d  evenlines:%d oddlines:%d\n",mx,my,md,mt,mbt);}}
printf("%d MBs differ\n",n);return 0;}
