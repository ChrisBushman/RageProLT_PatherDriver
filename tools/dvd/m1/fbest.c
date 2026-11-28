#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long ys=W*H,fs=ys*3/2;int nr=atoi(v[3]);FILE*a=fopen(v[1],"rb"),*b=fopen(v[2],"rb");unsigned char*R=malloc(fs*nr),*A=malloc(ys);fread(R,1,fs*nr,b);
for(int k=4;k<c;k++){int t=atoi(v[k]);fseek(a,t*ys,0);fread(A,1,ys,a);printf("tap %d:",t);
 for(int par=0;par<2;par++){long best=-1;int bi=0;for(int r=0;r<nr;r++){long s=0;for(int y=par;y<H;y+=2)for(int x=0;x<W;x+=3)s+=abs(A[y*W+x]-R[r*fs+y*W+x]);if(best<0||s<best){best=s;bi=r;}}
  printf("  %s field best ref %d (sad %ld)",par?"bottom":"top",bi,best);}
 printf("\n");}return 0;}
