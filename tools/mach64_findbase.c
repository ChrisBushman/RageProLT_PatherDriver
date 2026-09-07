/*
 * mach64_findbase.c - find the kext __TEXT load base by scanning kernel memory (via the dual-mode
 * peek) for the peek handler's OWN signature at static 0x710c: [0x74808000,0x4082000C,0x7C03202E].
 * Once base is known, resolve the submit-related slots correctly:
 *   set_surface calls (IOATIRProGLContext this)->vt[0x5a8];  finish calls (ctx[0xb0])->vt[0x564].
 * Prints static vaddrs to name offline.  Headless.  Build: cc -O2 -std=gnu99 -o ~/findbase ~/findbase.c -framework IOKit
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
static io_connect_t gl;
static uint32_t rd(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,0x80000000u|a,&o); return o; }
static uint32_t ctxrd(uint32_t off){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,off,&o); return o; }
int main(void){
    uint32_t idx=0x14u; int16_t W=1024,H=768;
    io_connect_t sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H},{{0,0,W,H}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);

    uint32_t vptr=ctxrd(0), eng=ctxrd(0xb0);
    printf("ctx vptr=0x%08x eng=0x%08x\n",vptr,eng);
    /* scan for peek handler signature 0x74808000 @ (base+0x710c); base near vptr-~0x18000 */
    uint32_t found=0;
    for(uint32_t base=(vptr&0xfff00000u); base< (vptr|0x000fffffu); base+=0x1000u){
        if(rd(base+0x710c)==0x74808000u && rd(base+0x7110)==0x4082000Cu){ found=base; break; }
    }
    if(!found){ printf("base NOT found by signature scan near vptr\n"); IOServiceClose(gl);IOServiceClose(sf); return 1; }
    printf("*** kext __TEXT load base = 0x%08x ***\n",found);
    uint32_t B=found;
    printf("set_surface->this->vt[0x5a8]  runtime=0x%08x static=0x%06x\n",
           rd(vptr+0x5a8), rd(vptr+0x5a8)-B);
    printf("this->vt[0x5ac] process?      runtime=0x%08x static=0x%06x\n",
           rd(vptr+0x5ac), rd(vptr+0x5ac)-B);
    if(eng){
        uint32_t evptr=rd(eng+0);
        printf("engine vptr=0x%08x static=0x%06x\n",evptr,evptr-B);
        printf("finish->eng->vt[0x564]    runtime=0x%08x static=0x%06x\n",
               rd(evptr+0x564), rd(evptr+0x564)-B);
        printf("eng vt[0x5a8]             runtime=0x%08x static=0x%06x\n",
               rd(evptr+0x5a8), rd(evptr+0x5a8)-B);
        printf("eng vt[0x5ac]             runtime=0x%08x static=0x%06x\n",
               rd(evptr+0x5ac), rd(evptr+0x5ac)-B);
    }
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
