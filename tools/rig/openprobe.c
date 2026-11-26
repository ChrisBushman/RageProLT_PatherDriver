#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
int main(void){ io_service_t s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("ATIRagePro")); unsigned t; printf("svc=0x%x\n",(unsigned)s);
 for(t=0;t<4;t++){ io_connect_t c=0; kern_return_t kr=IOServiceOpen(s,mach_task_self(),t,&c); printf("type %u -> kr=0x%08x conn=0x%x\n",t,kr,(unsigned)c); if(!kr) IOServiceClose(c);} return 0; }
