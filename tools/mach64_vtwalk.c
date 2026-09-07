/*
 * mach64_vtwalk.c - resolve the LOAD-LINKED runtime vtable of IOATIRProGLContext using the
 * patched dual-mode peek (sel 14): peek(off) = *(ctx+off) [ctx-relative]; peek(0x80000000|addr)
 * = *(addr) [absolute].  ctx[0] = runtime vptr; the vtable is static __const 0x17698 (vptr =
 * +8 = 0x176a0), so load_base = vptr - 0x176a0.  Then each slot's runtime fn -> static vaddr =
 * fn - load_base, which we name offline via symbols.  Reveals what set_surface(0x5a8)/finish(
 * 0x564)/process_command_buffer(0x5ac) REALLY call.  Headless.
 * Build: cc -O2 -std=gnu99 -o ~/vtwalk ~/vtwalk.c -framework IOKit
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
static uint32_t peek(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,a,&o); return o; }
#define VTBASE 0x176a0u   /* IOATIRProGLContext vptr = __ZTV +8 (static) */
int main(void){
    uint32_t idx=0x14u; int16_t W=1024,H=768;
    io_connect_t sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H},{{0,0,W,H}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);

    uint32_t vptr=peek(0);                 /* ctx[0] = runtime vtable ptr */
    uint32_t base=vptr-VTBASE;             /* kext load base (static vaddr 0 -> base) */
    printf("ctx vptr=0x%08x  => kext load_base=0x%08x\n",vptr,base);
    printf("ctx[0xb0](engine ATIR3GLContext)=0x%08x  ctx[0xdc](cmdbuf)=0x%08x  ctx[0x10c](surf)=0x%08x\n",
           peek(0xb0),peek(0xdc),peek(0x10c));
    printf("IOATIRProGLContext runtime vtable slots -> static vaddr (name offline):\n");
    for(uint32_t slot=0x558; slot<=0x5b4; slot+=4){
        uint32_t fn=peek(0x80000000u | (vptr+slot));
        if(!fn) continue;
        printf("  vt[0x%03x] runtime=0x%08x static=0x%06x\n",slot,fn,fn-base);
    }
    /* also resolve the ATIR3GLContext engine object's vtable (ctx[0xb0][0]) */
    uint32_t eng=peek(0xb0);
    if(eng){
        uint32_t evptr=peek(0x80000000u|(eng+0));
        printf("engine vptr=0x%08x (static 0x%06x); slots:\n",evptr,evptr-base);
        for(uint32_t slot=0x558; slot<=0x5b4; slot+=4){
            uint32_t fn=peek(0x80000000u|(evptr+slot));
            if(!fn) continue;
            printf("  eng vt[0x%03x] static=0x%06x\n",slot,fn-base);
        }
    }
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
