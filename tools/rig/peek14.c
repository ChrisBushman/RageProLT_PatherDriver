#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
int main(void){
  io_service_t s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("ATIRagePro"));
  if(!s){printf("no ATIRagePro\n");return 1;}
  io_connect_t c=0; kern_return_t kr=IOServiceOpen(s,mach_task_self(),1,&c);
  printf("open type1 kr=0x%x conn=%d\n",kr,(int)c); if(kr) return 1;
  unsigned offs[]={0xb0,0x10c,0xdc};
  int i; for(i=0;i<3;i++){ unsigned out=0xdeadbeef; kr=IOConnectMethodScalarIScalarO(c,14,1,1,offs[i],&out); printf("peek ctx[0x%x] kr=0x%x out=0x%08x\n",offs[i],kr,out);}
  IOServiceClose(c); return 0; }
