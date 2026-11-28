#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long fs=W*H*3/2;FILE*a=fopen(v[1],"rb"),*b=fopen(v[2],"rb");int fa=atoi(v[3]),fb=atoi(v[4]),mx=atoi(v[5]),my=atoi(v[6]);
unsigned char*A=malloc(fs),*B=malloc(fs);fseek(a,fa*fs,0);fseek(b,fb*fs,0);fread(A,1,fs,a);fread(B,1,fs,b);
for(int y=0;y<16;y++){printf("line %3d: ",my*16+y);for(int x=0;x<16;x++){int i=(my*16+y)*W+mx*16+x;printf("%4d",A[i]-B[i]);}printf("\n");}return 0;}
