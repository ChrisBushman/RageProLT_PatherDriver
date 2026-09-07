/*
 * mach64_cmdcheck.c - now that sel-14 dispatches with this=ATIR3GLContext, ctx[0xdc] is the REAL
 * engine command buffer (kernel addr).  Determine: (1) does the type-2 userspace map alias
 * ctx[0xdc]?  (2) which op drains it (submit_context_buffer clears flags-bit0 / bumps stamp)?
 * We READ the kernel command buffer header directly via the absolute-peek, so we don't depend on
 * the map aliasing.  Headless.  Build: cc -O2 -std=gnu99 -o ~/cmdcheck ~/cmdcheck.c -framework IOKit
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
static io_connect_t sf,gl; static uint32_t idx=0x14u;
static uint32_t cr(uint32_t off){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,off,&o); return o; }      /* ctx-relative */
static uint32_t ka(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,0x80000000u|a,&o); return o; } /* absolute */
static void bind(void){
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,1024,768},{{0,0,1024,768}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);
}
static uint32_t CMDK;
static void hdr(const char*t){
    printf("  %-22s [kernel cmdbuf] cap=%u count=%u flags=0x%x stamp=%u\n",
           t, ka(CMDK+0x10), ka(CMDK+0x14), ka(CMDK+0x1c), ka(CMDK+0x18));
}
int main(void){
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    bind();
    CMDK=cr(0xdc);
    printf("this(ATIR3GLContext) engine ctx[0xb0]=0x%08x surf ctx[0x10c]=0x%08x cmdbuf ctx[0xdc]=0x%08x\n",
           cr(0xb0),cr(0x10c),CMDK);
    /* map type-2 and test aliasing with the kernel cmdbuf */
    vm_address_t cmd=0; vm_size_t csz=0;
    IOConnectMapMemory(gl,2u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache);
    volatile uint32_t *M=(volatile uint32_t*)cmd;
    if(M){ M[3]=0xA5A5A5A5u;
        printf("type2 map @0x%lx: wrote magic to M[3]; kernel cmdbuf[+0xc]=0x%08x  %s\n",
               (unsigned long)cmd, ka(CMDK+0xc),
               ka(CMDK+0xc)==0xA5A5A5A5u?"*** TYPE-2 MAP ALIASES ctx[0xdc] ***":"(NOT the same buffer)");
    }
    printf("probe which op drains the KERNEL cmdbuf (fill via map if aliased, else header-only):\n");
    /* arm: if aliased, write a packet; always set count+flags via map (if aliased it reaches kernel) */
    #define ARM() do{ if(M){ M[8]=0x01fc;M[9]=0;M[5]=2;M[7]|=1u; } }while(0)
    ARM(); IOConnectMethodScalarIStructureI(gl,8u,0,0);                       hdr("finish(8)");
    ARM(); { uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);} hdr("set_surface(0)");
    ARM(); IOConnectMethodScalarIScalarO(gl,9u,1,0,0u);                       hdr("wait_for_stamp(9)");
    ARM(); { uint32_t a=0; IOConnectMethodScalarIScalarO(gl,4u,0,1,&a);}      hdr("get_status(4)");
    ARM(); IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u);                   hdr("surface_flush(10)");
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
