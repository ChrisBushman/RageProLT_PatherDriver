/*
 * mach64_vramrb.c - headless VRAM readback: bind surface+GL, map the VRAM aperture, engine-fill a
 * tag at surf[0x12c] offset (0x478000), then CPU-read VRAM there to prove whether the 2D engine
 * (via Apple's command buffer) actually writes that VRAM offset.  Runs over ssh (no present).
 * Build: cc -O2 -std=gnu99 -o ~/vramrb ~/vramrb.c -framework IOKit
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
static uint32_t peek(io_connect_t gl,uint32_t field){
    uint32_t out=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,field,&out); return out;
}
int main(void){
    uint32_t idx=0x14u; int16_t W=1024,H=768;
    io_connect_t sf=openc(0u), gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H},{{0,0,W,H}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);
    { uint32_t sd[32]; IOByteCount ol=sizeof sd; memset(sd,0,sizeof sd);
      IOConnectMethodScalarIStructureO(sf,14u,0,&ol,sd);
      IOConnectMethodScalarIScalarO(sf,15u,0,0);
      IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u); }

    /* get_surface_size (sel 5) may return the VRAM render offset */
    { uint32_t a=0,b=0,c=0,d=0;
      kern_return_t k=IOConnectMethodScalarIScalarO(gl,5u,0,4,&a,&b,&c,&d);
      printf("get_surface_size -> 0x%08x  [%08x %08x %08x %08x]\n",k,a,b,c,d); }

    uint32_t rt=peek(gl,0x12c);
    uint32_t off=(rt&0x3fffffu)<<3;
    printf("surf[0x12c] off_pitch=0x%08x -> VRAM byte off=0x%x\n",rt,off);

    /* find the VRAM aperture (>=4MB) across GL + surface map types */
    vm_address_t vram=0; vm_size_t vsz=0; io_connect_t vc=0; unsigned vt=0;
    for(unsigned which=0;which<2 && !vram;which++){
        io_connect_t cn = which? sf : gl;
        for(unsigned t=0;t<6;t++){
            vm_address_t a=0; vm_size_t s=0;
            if(!IOConnectMapMemory(cn,t,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache) && a){
                printf("  %s type %u -> addr=0x%lx size=0x%lx%s\n", which?"SURF":"GL",t,
                       (unsigned long)a,(unsigned long)s, (s>=0x400000)?"  <== VRAM?":"");
                if(s>=0x400000 && !vram){ vram=a; vsz=s; vc=cn; vt=t; }
                else IOConnectUnmapMemory(cn,t,mach_task_self(),a);
            }
        }
    }
    if(!vram){ printf("no VRAM aperture found\n"); return 1; }
    printf("VRAM aperture @0x%lx size=0x%lx\n",(unsigned long)vram,(unsigned long)vsz);
    volatile uint32_t *V=(volatile uint32_t*)vram;

    /* map cmd region + engine-fill a TAG (0xCAFE0000) 64x64 at VRAM off */
    vm_address_t cmd=0; vm_size_t csz=0;
    if(IOConnectMapMemory(gl,1u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache)||!cmd){
        printf("no cmd region\n"); return 1; }
    volatile uint32_t *hdr=(volatile uint32_t*)cmd, *body=hdr+8;
    printf("VRAM[off] BEFORE fill = %08x %08x\n", (off/4<vsz/4)?V[off/4]:0, (off/4+1<vsz/4)?V[off/4+1]:0);
    unsigned n=0;
    #define P(r,v) do{ body[n++]=(uint32_t)(r); body[n++]=(uint32_t)(v);}while(0)
    P(0x054c,0);P(0x05fc,0);P(0x0730,3);P(0x06d0,0x60060666u);
    P(0x06c4,0xCAFE0000u);P(0x06c8,0xffffffffu);P(0x06d4,0x00070003u);P(0x06d8,0x00000101u);
    P(0x06a8,0x1fff0000u);P(0x06b4,0x1fff0000u);P(0x0708,0);
    P(0x0500,rt);
    P(0x06e8,0);P(0x06ec,(64u<<16)|64u);
    #undef P
    hdr[5]=n; hdr[7]|=1u;
    kern_return_t k=IOConnectMethodScalarIStructureI(gl,8u,0,0);
    printf("engine fill(0xCAFE0000 @0x%x) finish -> 0x%08x\n",off,k);
    printf("VRAM[off]  AFTER fill = %08x %08x  (== CAFE0000 => engine writes surf[0x12c] VRAM!)\n",
           (off/4<vsz/4)?V[off/4]:0, (off/4+1<vsz/4)?V[off/4+1]:0);

    /* also scan the whole aperture for our tag, to find where the engine ACTUALLY wrote */
    uint32_t hits=0;
    for(uint32_t i=0;i<vsz/4 && hits<6;i++){
        if(V[i]==0xCAFE0000u){ printf("  TAG found at VRAM byte 0x%x (off_pitch off field=0x%x)\n",
                                       i*4, (i*4)>>3); hits++; }
    }
    if(!hits) printf("  TAG not found anywhere in VRAM aperture (engine wrote AGP/elsewhere, or not run)\n");
    IOConnectUnmapMemory(gl,1u,mach_task_self(),cmd);
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n");
    return 0;
}
