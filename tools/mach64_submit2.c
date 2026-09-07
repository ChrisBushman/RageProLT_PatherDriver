/*
 * mach64_submit2.c - test the DISCOVERED submit trigger: submit_context_buffer (the drain) is
 * called from clientMemoryForType, i.e. from IOConnectMapMemory.  Fill the type-2 command buffer,
 * then IOConnectMapMemory(type 1) and check whether the kernel command buffer drained (flags-bit0
 * clears / stamp bumps / count resets).  Read the kernel cmdbuf header via absolute-peek.  Headless.
 * Build: cc -O2 -std=gnu99 -o ~/submit2 ~/submit2.c -framework IOKit
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#pragma pack(push,2)
typedef struct { int16_t x,y,w,h; } Bounds;
typedef struct { uint32_t n; Bounds bounds; Bounds rect[1]; } Region;
#pragma pack(pop)
static io_connect_t openc(unsigned t){
    io_service_t s=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("ATIRagePro"));
    if(!s){printf("no ATIRagePro\n");return 0;}
    io_connect_t c=0; if(IOServiceOpen(s,mach_task_self(),t,&c)){IOObjectRelease(s);return 0;}
    IOObjectRelease(s); return c;
}
static io_connect_t sf,gl; static uint32_t idx=0x14u; static uint32_t CMDK; static volatile uint32_t*M;
static uint32_t cr(uint32_t off){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,off,&o); return o; }
static uint32_t ka(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,0x80000000u|a,&o); return o; }
static void fill(void){ M[8]=0x02c4;M[9]=0x00ff0000u;M[10]=0x0100;M[11]=cr(0x12c)?0:0; M[5]=4; M[7]|=1u; }
static void hdr(const char*t){
    printf("  %-28s cmdbuf: count=%u flags=0x%x stamp=%u  M(user)count=%u flags=0x%x\n",
           t, ka(CMDK+0x14),ka(CMDK+0x1c),ka(CMDK+0x18), M[5],M[7]);
}
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,1024,768},{{0,0,1024,768}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);
    vm_address_t cmd=0; vm_size_t csz=0;
    IOConnectMapMemory(gl,2u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache);
    M=(volatile uint32_t*)cmd; CMDK=cr(0xdc);
    printf("cmdbuf ctx[0xdc]=0x%08x  cap=%u\n",CMDK,ka(CMDK+0x10));

    fill(); hdr("after fill");
    /* THE candidate trigger: map type 1 (surface buffer) -> clientMemoryForType -> submit drain */
    { vm_address_t a=0; vm_size_t s=0;
      kern_return_t k=IOConnectMapMemory(gl,1u,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache);
      printf("  IOConnectMapMemory(type1) kr=0x%x addr=0x%lx size=0x%lx\n",k,(unsigned long)a,(unsigned long)s);
      hdr("after map type1");
      if(a) IOConnectUnmapMemory(gl,1u,mach_task_self(),a); }
    fill(); { vm_address_t a=0; vm_size_t s=0;
      IOConnectMapMemory(gl,4u,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache);
      hdr("after map type4"); if(a) IOConnectUnmapMemory(gl,4u,mach_task_self(),a); }
    fill(); { vm_address_t a=0; vm_size_t s=0;
      IOConnectMapMemory(gl,2u,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache);
      hdr("after remap type2"); if(a) IOConnectUnmapMemory(gl,2u,mach_task_self(),a); }
    fill(); { vm_address_t a=0; vm_size_t s=0;
      IOConnectMapMemory(sf,0u,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache);
      hdr("after map surf type0"); if(a) IOConnectUnmapMemory(sf,0u,mach_task_self(),a); }
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
