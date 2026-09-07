/*
 * mach64_base.c - pin the kext load base by anchoring on our OWN peek handler (sel-14 method =
 * static 0x710c): base = methodDesc[14].func - 0x710c.  Then resolve this(ATIR3GLContext)'s
 * runtime vtable slots to exact static vaddrs so we can NAME what set_surface(vt 0x5a8),
 * process(0x5ac), invalidate(0x59c), update_surface(0x5a0) really call.  Headless.
 * Build: cc -O2 -std=gnu99 -o ~/base ~/base.c -framework IOKit
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
static uint32_t cr(uint32_t off){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,off,&o); return o; }
static uint32_t ka(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,0x80000000u|a,&o); return o; }
int main(void){
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,1024,768},{{0,0,1024,768}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t d=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&d);

    uint32_t vptr=cr(0), tbl=cr(0x11c);
    printf("vptr=0x%08x methodDescTable ctx[0x11c]=0x%08x\n",vptr,tbl);
    /* methodDesc stride 0x18: {object,+0; func,+4; ... }. sel14 func = our peek @static 0x710c */
    uint32_t e14=tbl+14*0x18;
    uint32_t f4=ka(e14+4), f0=ka(e14+0), f8=ka(e14+8);
    printf("methodDesc[14]: +0=0x%08x +4=0x%08x +8=0x%08x\n",f0,f4,f8);
    /* the peek handler ptr is whichever of +0/+4/+8 is a code addr; base = that - 0x710c */
    uint32_t base=0, cand[3]={f0,f4,f8};
    for(int i=0;i<3;i++){ if(cand[i]>vptr-0x30000 && cand[i]<vptr){ base=cand[i]-0x710c; break; } }
    printf("*** derived kext base = 0x%08x (peek handler runtime = base+0x710c) ***\n",base);
    if(!base){ printf("could not derive base\n"); goto done; }
    printf("this(ATIR3GLContext) runtime vt -> static (kext-range only):\n");
    for(uint32_t slot=0x558; slot<=0x5b4; slot+=4){
        uint32_t fn=ka(vptr+slot);
        if(fn>=base && fn<base+0x16700) printf("  vt[0x%03x] static=0x%06x\n",slot,fn-base);
    }
    uint32_t eng=cr(0xb0);
    printf("engine ctx[0xb0]=0x%08x vtable=0x%08x; runtime vt -> static:\n",eng,ka(eng+0));
    uint32_t evptr=ka(eng+0);
    for(uint32_t slot=0x558; slot<=0x5b4; slot+=4){
        uint32_t fn=ka(evptr+slot);
        if(fn>=base && fn<base+0x16700) printf("  eng vt[0x%03x] static=0x%06x\n",slot,fn-base);
    }
done:
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n"); return 0;
}
