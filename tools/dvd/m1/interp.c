#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long ys=W*H;FILE*a=fopen(v[1],"rb");int t=atoi(v[2]);unsigned char*A=malloc(ys);fseek(a,t*ys,0);fread(A,1,ys,a);
long s_avg=0,s_up=0,s_dn=0,n=0;int mx=0;for(int y=1;y<H-1;y+=2)for(int x=0;x<W;x++){int b=A[y*W+x],u=A[(y-1)*W+x],d=A[(y+1)*W+x];s_avg+=abs(b-(u+d+1)/2);s_up+=abs(b-u);s_dn+=abs(b-d);n++;}
printf("tap %d bottom-line vs top neighbours: mean|b-avg(up,down)|=%.3f  |b-up|=%.3f |b-down|=%.3f\n",t,s_avg/(double)n,s_up/(double)n,s_dn/(double)n);return 0;}
