/* cmp2 ours.yuv ref.yuv nframes [window]: for each dumped frame find the best-matching reference frame (display order may differ from decode order) */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define W 720
#define H 480
#define FS (W*H*3/2)
int main(int argc,char**argv){
  FILE*fa=fopen(argv[1],"rb"),*fr=fopen(argv[2],"rb"); int nf=atoi(argv[3]); int win=argc>4?atoi(argv[4]):8;
  static unsigned char a[FS]; unsigned char *R; int nref,i,j,k;
  fseek(fr,0,SEEK_END); nref=ftell(fr)/FS; rewind(fr); R=malloc((long)nref*FS); fread(R,1,(long)nref*FS,fr);
  int worst_maxd=0; double minp=999; int bad=0;
  for(i=0;i<nf;i++){
    if(fread(a,1,FS,fa)!=FS) break;
    int best=-1; long bse=0; int bmx=0;
    for(j=i-win;j<=i+win;j++){ if(j<0||j>=nref) continue;
      long se=0; int mx=0; unsigned char*r=R+(long)j*FS;
      for(k=0;k<FS;k++){int d=a[k]-r[k]; if(d<0)d=-d; se+=d*d; if(d>mx)mx=d;}
      if(best<0||se<bse){best=j;bse=se;bmx=mx;} }
    double p=bse?10*log10(255.0*255*FS/bse):99.0; if(p<minp)minp=p; if(bmx>worst_maxd)worst_maxd=bmx; if(bmx>3||p<50)bad++;
    if(argc>5 || bad<=5 && (bmx>3||p<50)) printf("dump %3d -> ref %3d psnr %6.2f maxdiff %3d\n",i,best,p,bmx);
  }
  printf("summary: %d frames, min psnr %.2f, worst maxdiff %d, frames with maxdiff>3 or psnr<50: %d\n",i,minp,worst_maxd,bad);
  return 0;}
