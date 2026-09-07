/*
 * mach64_submitprobe.c - find which selector DRAINS the GL command buffer (ctx[0xdc], map type 2).
 * Fill it with a tiny reg-write, set count+flags, then call candidate selectors; after each, read
 * the header -- flags bit0 CLEARED (and/or stamp bumped) => that selector submitted it.  Headless.
 * Build: cc -O2 -std=gnu99 -o ~/submitprobe ~/submitprobe.c -framework IOKit
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
static volatile uint32_t *H;
static uint32_t idx=0x14u; static io_connect_t sf,gl;
static void fillcmd(void){                 /* one harmless reg write: SCALE_3D_CNTL=0 */
    H[8]=0x01fc; H[9]=0; H[5]=2; H[7]|=1u;  /* body[0..1], count=2, flags|=SUBMIT */
}
static void show(const char*tag){ printf("  %-22s -> count=%u flags=0x%x stamp=%u\n",tag,H[5],H[7],H[6]); }
int main(void){
    int16_t W=1024,H_=768;
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H_},{{0,0,W,H_}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);
    vm_address_t cmd=0; vm_size_t csz=0;
    if(IOConnectMapMemory(gl,2u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache)||!cmd){
        printf("no cmd\n"); return 1; }
    H=(volatile uint32_t*)cmd;
    printf("cmd @0x%lx cap=%u count=%u flags=0x%x stamp=%u\n",(unsigned long)cmd,H[4],H[5],H[7],H[6]);

    fillcmd(); show("after fill");
    IOConnectMethodScalarIStructureI(gl,8u,0,0);                       show("finish(8)");
    fillcmd(); IOConnectMethodScalarIScalarO(gl,9u,1,0,0u);            show("wait_for_stamp(9)");
    fillcmd(); { uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);} show("set_surface(0)");
    fillcmd(); IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);     show("surf set_id_mode(7)");
    fillcmd(); { uint32_t a,b,c; IOConnectMethodScalarIScalarO(gl,4u,0,1,&a); (void)b;(void)c;} show("get_status(4)");
    fillcmd(); IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u);        show("surface_flush(sf10)");
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
