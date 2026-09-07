/*
 * mach64_trace.c - LIVE trace of the GL command-buffer drain.  The patched kext stamps
 * buffer[+4]=0xDEADBEEF at the entry of submit_context_buffer (the drain).  We map the type-2
 * command buffer, then after each candidate operation check word[1] (+0x04): if it's 0xDEADBEEF,
 * that operation caused submit_context_buffer to run on OUR buffer -> the real submit trigger.
 * Headless (no present).  Build: cc -O2 -std=gnu99 -o ~/trace ~/trace.c -framework IOKit
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
static volatile uint32_t *B;   /* mapped command buffer */
static io_connect_t sf,gl; static uint32_t idx=0x14u;
#define MAGIC 0xDEADBEEFu
static void arm(void){                 /* reset stamp + put a minimal packet + count + SUBMIT flag */
    B[1]=0;                             /* clear the +0x04 stamp word */
    B[8]=0x01fc; B[9]=0; B[5]=2; B[7]|=1u;
}
static int fired(const char*tag){
    int hit = (B[1]==MAGIC);
    printf("  %-26s stamp[+4]=%08x count=%u flags=0x%x stamp=%u  %s\n",
           tag,B[1],B[5],B[7],B[6], hit?"<== DRAINED OUR BUFFER":"");
    return hit;
}
int main(void){
    int16_t W=1024,H=768;
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H},{{0,0,W,H}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);
    vm_address_t cmd=0; vm_size_t csz=0;
    if(IOConnectMapMemory(gl,2u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache)||!cmd){
        printf("no cmd buffer\n"); return 1; }
    B=(volatile uint32_t*)cmd;
    printf("cmd buffer @0x%lx cap=%u count=%u flags=0x%x stamp=%u  word[1]=%08x\n",
           (unsigned long)cmd,B[4],B[5],B[7],B[6],B[1]);

    printf("probing which op drains OUR buffer (submit_context_buffer stamps 0xDEADBEEF):\n");
    arm(); IOConnectMethodScalarIStructureI(gl,8u,0,0);                        fired("finish(8)");
    arm(); IOConnectMethodScalarIScalarO(gl,9u,1,0,0u);                        fired("wait_for_stamp(9)");
    arm(); { uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);} fired("set_surface(0)");
    arm(); { uint32_t a=0; IOConnectMethodScalarIScalarO(gl,4u,0,1,&a);}       fired("get_status(4)");
    arm(); { uint32_t a=0,b=0,c=0; IOConnectMethodScalarIScalarO(gl,3u,0,3,&a,&b,&c);} fired("get_config(3)");
    arm(); { uint32_t a=0,b=0,c=0,dd=0; IOConnectMethodScalarIScalarO(gl,5u,0,4,&a,&b,&c,&dd);} fired("get_surface_size(5)");
    arm(); IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);                 fired("surf set_id_mode(7)");
    arm(); IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u);                    fired("surface_flush(sf10)");
    arm(); { uint32_t a=0,b=0,c=0; IOConnectMethodScalarIScalarO(gl,6u,1,3,idx,&a,&b,&c);} fired("get_surface_info(6)");
    /* combo: many drivers submit on the NEXT set_surface after filling */
    arm(); { uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);
             IOConnectMethodScalarIStructureI(gl,8u,0,0);} fired("set_surface+finish");

    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
