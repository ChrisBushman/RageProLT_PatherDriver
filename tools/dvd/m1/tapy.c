/* tapy dir nfiles parity out.y : concatenate luma planes of 720x480 4:2:2 uploads (parity = byte offset of Y within each 2-byte pair) */
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int n=atoi(v[2]),par=atoi(v[3]);FILE*o=fopen(v[4],"wb");unsigned char*b=malloc(736*480*2),*y=malloc(720*480);char fn[512];
for(int i=0;i<n;i++){sprintf(fn,"%s/f%04d.yuv",v[1],i);FILE*f=fopen(fn,"rb");if(!f)break;fread(b,1,736*480*2,f);fclose(f);for(int r=0;r<480;r++)for(int x=0;x<720;x++)y[r*720+x]=b[(r*736+x)*2+par];fwrite(y,1,720*480,o);}return 0;}
