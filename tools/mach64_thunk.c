/*
 * mach64_thunk.c - resolve relocated `lis r12,HI; ori r12,r12,LO; bctr` thunks at RUNTIME via the
 * dual-mode peek, to follow the command-drain call chain the disassembler can't (thunk highs are
 * filled at kext load).  Anchors base on our peek handler (static 0x710c = methodDesc[14].func).
 * Prints each thunk's real target as a static vaddr to disassemble offline.  Headless.
 * Build: cc -O2 -std=gnu99 -o ~/thunk ~/thunk.c -framework IOKit
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
static io_connect_t sf,gl; static uint32_t base;
static uint32_t cr(uint32_t off){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,off,&o); return o; }
static uint32_t ka(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,0x80000000u|a,&o); return o; }
/* resolve `lis r12,HI; ori r12,r12,LO` at static addr sv -> real target static vaddr */
static uint32_t thunk(uint32_t sv){
    uint32_t i0=ka(base+sv), i1=ka(base+sv+4);
    uint32_t hi=i0&0xffff, lo=i1&0xffff, tgt=(hi<<16)|lo;   /* runtime target */
    printf("  thunk@0x%06x: lis=0x%08x ori=0x%08x -> runtime 0x%08x static 0x%06x\n",
           sv,i0,i1,tgt,tgt-base);
    return tgt-base;
}
int main(void){
    uint32_t idx=0x14u;
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,1024,768},{{0,0,1024,768}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);
    uint32_t vptr=cr(0), tbl=cr(0x11c), f=ka(tbl+14*0x18+8);   /* methodDesc[14].func (peek@0x710c) */
    base=f-0x710c;
    printf("vptr=0x%08x base=0x%08x (peek func runtime=0x%08x)\n",vptr,base,f);
    if(base>0xf0000000u||!base){ printf("bad base\n"); goto done; }
    printf("resolving setup2D / engine thunks:\n");
    thunk(0x2b38);    /* setup2D -> real worker */
    thunk(0x2b6c);    /* setup2D variant */
    thunk(0x2ba0);
    thunk(0x13b5c);   /* submit_context_buffer -> submit_buffer(should be 0x12730) */
done:
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
