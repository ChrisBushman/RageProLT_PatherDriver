#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long ys=W*H,fs=ys*3/2;FILE*a=fopen(v[1],"rb"),*b=fopen(v[2],"rb");int fa=atoi(v[3]),fb=atoi(v[4]);
unsigned char*A=malloc(ys),*B=malloc(fs);fseek(a,fa*ys,0);fread(A,1,ys,a);fseek(b,fb*fs,0);fread(B,1,ys,b);long s[2]={0,0};int mx[2]={0,0};
for(int y=0;y<H;y++)for(int x=0;x<W;x++){int d=abs(A[y*W+x]-B[y*W+x]);s[y&1]+=d;if(d>mx[y&1])mx[y&1]=d;}
printf("tap %d vs ref %d: mean|d| top field %.2f (max %d), bottom field %.2f (max %d)\n",fa,fb,s[0]/(double)(ys/2),mx[0],s[1]/(double)(ys/2),mx[1]);return 0;}
