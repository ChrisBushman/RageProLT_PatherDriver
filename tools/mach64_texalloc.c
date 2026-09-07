/*
 * mach64_texalloc.c - probe IOATIRProGLContext::new_texture (GL sel 10, StructI->StructO) to see
 * if it hands back a VRAM offset we can use as a render target (DST_OFF_PITCH).
 *
 * new_texture is an 8-way dispatch on input[0]:
 *   sub 7 -> IOATIRProShared::new_texture       (VRAM texture)   <- want this
 *   sub 6 -> new_agpref_texture (AGP/sysmem)
 *   sub 1 -> new_global_texture
 * sub 7 handler: new_texture(shared, in[4], in[8], in[0xc], in[0x10], &out[0], &out[4]).
 * We try a few input shapes and dump the output dwords -- a small 8-aligned value (< VRAM size)
 * that scales with W*H*bpp is the VRAM offset.  Console/ssh, sudo.
 * Build: cc -O2 -std=gnu99 -o /tmp/texalloc mach64_texalloc.c -framework IOKit
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
    io_connect_t c=0; kern_return_t k=IOServiceOpen(s,mach_task_self(),t,&c); IOObjectRelease(s);
    if(k){printf("open type %u FAILED 0x%08x\n",t,k);return 0;} return c;
}

int main(void){
    uint32_t idx=0x14u, mode=0x24u; int16_t W=1024,H=768;
    io_connect_t sf=openc(0u), gl=openc(1u);
    if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,mode);
    Region rg; rg.n=1; rg.bounds.x=0;rg.bounds.y=0;rg.bounds.w=W;rg.bounds.h=H; rg.rect[0]=rg.bounds;
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0;
    IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);
    /* map type-1 (command region) + type-2 (context buffer): these init the context's buffers
     * + surface page-queue, which new_texture's allocator likely needs. */
    vm_address_t a1=0,a2=0; vm_size_t s1=0,s2=0;
    kern_return_t m1=IOConnectMapMemory(gl,1u,mach_task_self(),&a1,&s1,kIOMapAnywhere|kIOMapInhibitCache);
    kern_return_t m2=IOConnectMapMemory(gl,2u,mach_task_self(),&a2,&s2,kIOMapAnywhere|kIOMapInhibitCache);
    printf("surface bound.  map type1=0x%x(0x%x) type2=0x%x(0x%x).  probing new_texture...\n",
           (unsigned)m1,(unsigned)s1,(unsigned)m2,(unsigned)s2);

    /* try sub-op 7 (VRAM) and 6 (AGP) with a few {W,H,fmt,flags} shapes */
    unsigned subops[]={7,6};
    /* new_texture requires arg3(in[0xc]) AND arg4(in[0x10]) non-zero. Try {W,H,fmt,flags}. */
    struct { uint32_t w,h,fmt,flags; } shapes[]={
        {256,256,6,1},{256,256,6,4},{256,256,6,256*4},{256,256,4,1},{64,64,6,1}
    };
    unsigned si,sh;
    for(si=0;si<2;si++){
        for(sh=0;sh<5;sh++){
            uint32_t in[8]; memset(in,0,sizeof in);
            in[0]=subops[si]; in[1]=shapes[sh].w; in[2]=shapes[sh].h;
            in[3]=shapes[sh].fmt; in[4]=shapes[sh].flags;
            uint32_t out[16]; memset(out,0,sizeof out);
            IOByteCount osz=sizeof out;
            kern_return_t k=IOConnectMethodStructureIStructureO(gl,10u,sizeof in,&osz,in,out);
            printf("  sub %u {W=%u H=%u fmt=%u fl=%u} -> kr=0x%08x osz=%u  out: %08x %08x %08x %08x %08x %08x\n",
                   subops[si],in[1],in[2],in[3],in[4],(unsigned)k,(unsigned)osz,
                   out[0],out[1],out[2],out[3],out[4],out[5]);
        }
    }
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done\n");
    return 0;
}
