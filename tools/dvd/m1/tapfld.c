/* tapfld tap.y ntap ref.yuv nref parity : per tap: best ref by SAD on lines of given parity; list 16x16 MBs (frame coords) whose max diff on those lines > 6 */
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long ys=W*H,fs=ys*3/2;int nt=atoi(v[2]),nr=atoi(v[4]),par=atoi(v[5]);
FILE*f=fopen(v[1],"rb");unsigned char*T=malloc(ys*nt);fread(T,1,ys*nt,f);f=fopen(v[3],"rb");unsigned char*R=malloc(fs*nr);fread(R,1,fs*nr,f);
for(int t=0;t<nt;t++){long best=-1;int bi=0;for(int r=0;r<nr;r++){long s=0;for(int y=par;y<H;y+=2)for(int x=0;x<W;x+=3)s+=abs(T[t*ys+y*W+x]-R[r*fs+y*W+x]);if(best<0||s<best){best=s;bi=r;}}
 printf("tap %2d -> ref %2d  MBs:",t,bi);int n=0;for(int my=0;my<H/16;my++)for(int mx=0;mx<W/16;mx++){int md=0;for(int y=par;y<16;y+=2)for(int x=0;x<16;x++){int i=(my*16+y)*W+mx*16+x;int d=abs(T[t*ys+i]-R[bi*fs+i]);if(d>md)md=d;}if(md>6){if(n<6)printf(" (%d,%d)=%d",mx,my,md);n++;}}printf("  [%d MBs]\n",n);}
return 0;}
