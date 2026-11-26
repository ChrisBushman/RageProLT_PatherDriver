#include <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <unistd.h>
int main(int c, char **v){ int x0=atoi(v[1]),y0=atoi(v[2]),x1=atoi(v[3]),y1=atoi(v[4]),i; CGPoint p; 
 p.x=x0;p.y=y0; CGPostMouseEvent(p,1,1,0); usleep(100000); CGPostMouseEvent(p,0,1,1); usleep(100000);
 for(i=1;i<=20;i++){ p.x=x0+(x1-x0)*i/20; p.y=y0+(y1-y0)*i/20; CGPostMouseEvent(p,1,1,1); usleep(30000);} 
 usleep(100000); CGPostMouseEvent(p,1,1,0); return 0; }
