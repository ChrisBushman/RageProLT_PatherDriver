/* tapcmp tap.y ntap ref.yuv nref : per tap frame: best ref frame (luma SAD) and number of 16x16 luma MBs with maxdiff>2; prints MB list for frames given as extra args "ref:N" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int c,char**v){int W=720,H=480;long ys=W*H,fs=ys*3/2;int nt=atoi(v[2]),nr=atoi(v[4]);
FILE*f=fopen(v[1],"rb");unsigned char*T=malloc(ys*nt);fread(T,1,ys*nt,f);f=fopen(v[3],"rb");unsigned char*R=malloc(fs*nr);fread(R,1,fs*nr,f);
int seen[512];memset(seen,0,sizeof seen);
for(int t=0;t<nt;t++){long best=-1;int bi=0;for(int r=0;r<nr;r++){long s=0;for(long i=0;i<ys;i+=7)s+=abs(T[t*ys+i]-R[r*fs+i]);if(best<0||s<best){best=s;bi=r;}}
 int nb=0,mx=0;for(int my=0;my<H/16;my++)for(int mx2=0;mx2<W/16;mx2++){int md=0;for(int y=0;y<16;y++)for(int x=0;x<16;x++){int i=(my*16+y)*W+mx2*16+x;int d=abs(T[t*ys+i]-R[bi*fs+i]);if(d>md)md=d;}if(md>2){nb++;if(md>mx)mx=md;}}
 if(!seen[bi]){seen[bi]=1;printf("tap %3d -> ref %2d  sad(sampled)=%ld  MBs>2: %d (max %d)\n",t,bi,best,nb,mx);}}
return 0;}
