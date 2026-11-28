/* srch ref.yuv target_frame mbx field_mb_row(r0 = first field row of the 16x8) parity  : find exact integer-pel source of the 16x8 block in ffmpeg's decode */
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){int W=720,H=480;long fs=W*H*3/2;FILE*f=fopen(v[1],"rb");fseek(f,0,2);int nf=ftell(f)/fs;rewind(f);unsigned char*D=malloc(fs*nf);fread(D,1,fs*nf,f);
int tf=atoi(v[2]),x0=atoi(v[3])*16,r0=atoi(v[4]),par=atoi(v[5]);
for(int F=0;F<nf;F++)for(int sp=0;sp<2;sp++)for(int dy=-40;dy<=40;dy++)for(int dx=-80;dx<=80;dx++){
 int ok=1;for(int r=0;r<8&&ok;r++)for(int x=0;x<16;x++){int sy=r0+r+dy,sx=x0+x+dx;if(sy<0||sy>=H/2||sx<0||sx>=W){ok=0;break;}
  int t=D[tf*fs+(2*(r0+r)+par)*W+x0+x];int s=D[F*fs+(2*sy+sp)*W+sx];if(t!=s){ok=0;break;}}
 if(ok)printf("match: frame %d parity %d dx %d dy %d (half-pel mv %d,%d)\n",F,sp,dx,dy,2*dx,2*dy);}
return 0;}
