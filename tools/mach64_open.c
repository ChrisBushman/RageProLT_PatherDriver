#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <unistd.h>
#include "../common/mach64_abi.h"
int main(void){ io_service_t s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator")); io_connect_t c; if(!s||IOServiceOpen(s,mach_task_self(),MACH64_GL_CONNECT_TYPE,&c)){printf("open failed\n");return 1;} printf("open ok\n"); fflush(stdout); sleep(60); return 0; }
