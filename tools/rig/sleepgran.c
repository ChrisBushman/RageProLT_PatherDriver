#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <mach/thread_switch.h>
static double ms(uint64_t t){ static mach_timebase_info_data_t tb; if(!tb.denom) mach_timebase_info(&tb); return (double)t*tb.numer/tb.denom/1e6; }
int main(void){ int i; uint64_t t; double mn,mx,sum; int us[]={100,1000,3000,10000};
  for(int k=0;k<4;k++){ mn=1e9;mx=0;sum=0; for(i=0;i<100;i++){ t=mach_absolute_time(); usleep(us[k]); double d=ms(mach_absolute_time()-t); if(d<mn)mn=d; if(d>mx)mx=d; sum+=d; } printf("usleep(%d us): min %.2f mean %.2f max %.2f ms\n",us[k],mn,sum/100,mx); }
  mn=1e9;mx=0;sum=0; for(i=0;i<100;i++){ t=mach_absolute_time(); thread_switch(MACH_PORT_NULL,SWITCH_OPTION_WAIT,1); double d=ms(mach_absolute_time()-t); if(d<mn)mn=d; if(d>mx)mx=d; sum+=d; } printf("thread_switch(WAIT,1ms): min %.2f mean %.2f max %.2f ms\n",mn,sum/100,mx);
  mn=1e9;mx=0;sum=0; for(i=0;i<100;i++){ t=mach_absolute_time(); thread_switch(MACH_PORT_NULL,SWITCH_OPTION_DEPRESS,1); double d=ms(mach_absolute_time()-t); if(d<mn)mn=d; if(d>mx)mx=d; sum+=d; } printf("thread_switch(DEPRESS,1ms): min %.2f mean %.2f max %.2f ms\n",mn,sum/100,mx);
  return 0; }
