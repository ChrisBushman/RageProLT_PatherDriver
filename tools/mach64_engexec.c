/*
 * mach64_engexec.c - prove the engine EXECUTES our register writes: fill a big RED rect at VRAM
 * offset 0 (pitch 1024 = the scanout region) via the type-2 command buffer, submit by
 * IOConnectMapMemory(type1), NO surface_flush.  If the top-left of the screen turns red/changes,
 * the engine ran our commands (and offset 0 is visible).  Console, sudo (corrupts scanout; reboot
 * after).  Build: cc -O2 -std=gnu99 -o ~/engexec ~/engexec.c -framework IOKit
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
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
static io_connect_t sf,gl;
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    uint32_t idx=0x14u; int16_t W=1024,H=768;
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H},{{0,0,W,H}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);
    vm_address_t cmd=0; vm_size_t csz=0;
    if(IOConnectMapMemory(gl,2u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache)||!cmd){
        printf("no cmd buffer\n"); return 1; }
    volatile uint32_t *hdr=(volatile uint32_t*)cmd, *body=hdr+8;

    /* PACKET format: body[0] = (0x01<<24)|dwordcount (Rage128 style, count includes the header
     * word), then {reg,val} pairs.  RED fill 400x400 at DST_OFF_PITCH off=0 pitch=1024. */
    unsigned n=1;                          /* reserve body[0] for the packet header */
    #define P(r,v) do{ body[n++]=(uint32_t)(r); body[n++]=(uint32_t)(v);}while(0)
    P(0x01fc,0); P(0x02d0,0x60060666u);
    P(0x02c4,0x00ff0000u); P(0x02c8,0xffffffffu); P(0x02d4,0x00070003u); P(0x02d8,0x00000101u);
    P(0x02a8,0x1fff0000u); P(0x02b4,0x1fff0000u); P(0x0308,0);
    P(0x0100,(128u<<22)|0u); P(0x0130,3); P(0x0118,0); P(0x011c,(400u<<16)|400u);
    #undef P
    body[0]=0x01000000u | n;               /* type 1 (register-write), count = n dwords incl header */
    hdr[5]=n; hdr[7]|=1u;
    printf("packet body[0]=0x%08x, %u dwords total, count=%u flags=0x%x; submit via MapMemory(type1)...\n",
           body[0],n,hdr[5],hdr[7]);
    { vm_address_t a=0; vm_size_t s=0;
      IOConnectMapMemory(gl,1u,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache);
      printf("submit done, cmdbuf flags now 0x%x (0=drained)\n",hdr[7]);
      if(a) IOConnectUnmapMemory(gl,1u,mach_task_self(),a); }
    printf("LOOK NOW: RED 400x400 top-left = ENGINE EXECUTES.  holding 8s (NO flush)...\n");
    { struct timespec ts={8,0}; nanosleep(&ts,0); }
    IOConnectUnmapMemory(gl,2u,mach_task_self(),cmd);
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done (reboot to restore screen if corrupted).\n");
    return 0;
}
