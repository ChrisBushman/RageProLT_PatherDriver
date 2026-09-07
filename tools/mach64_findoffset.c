/*
 * mach64_findoffset.c - find the surface's VRAM buffer byte offset (for Path-B HW rendering).
 *
 * The write_lock backing is a CPU-readable mapping of the surface's VRAM buffer.  So: engine-fill
 * a TAGGED value at a grid of VRAM offsets via Apple's command buffer, then scan the backing for
 * the tag -- wherever it appears, that VRAM offset maps to backing[i], so the surface buffer's
 * VRAM base O = X - i*4.  Once known, DST_OFF_PITCH=O lets the engine render straight into the
 * surface, and surface_flush presents it = hardware-accelerated Path B.
 *
 * Corrupts VRAM (grid fills) -- reboot after.  Console, sudo.
 * Build: cc -O2 -std=gnu99 -o /tmp/findoffset mach64_findoffset.c -framework IOKit
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

static volatile uint32_t *g_hdr,*g_body; static io_connect_t g_gl;

/* engine 2D-fill: 16x1 px = tag at VRAM byte offset `off`, value `val` */
static void engine_fill(uint32_t off,uint32_t val){
    unsigned n=0;
    #define P(r,v) do{ g_body[n++]=(uint32_t)(r); g_body[n++]=(uint32_t)(v); }while(0)
    P(0x054c,0); P(0x05fc,0); P(0x0730,3); P(0x06d0,0x60060666u);
    P(0x06c4,val); P(0x06c8,0xffffffffu); P(0x06d4,0x00070003u); P(0x06d8,0x00000101u);
    P(0x06a8,0x1fff0000u); P(0x06b4,0x1fff0000u); P(0x0708,0);
    P(0x0500,(1u<<22)|(off>>3));       /* DST_OFF_PITCH pitch=8px off=off */
    P(0x06e8,0); P(0x06ec,(1u<<16)|16u);  /* 16x1 fill */
    #undef P
    g_hdr[5]=n; g_hdr[7]|=1u;
    IOConnectMethodScalarIStructureI(g_gl,8u,0,0);  /* finish -> drain */
}

int main(void){
    uint32_t idx=0x14u, mode=0x24u; int16_t W=1024,H=768;
    io_connect_t sf=openc(0u); g_gl=openc(1u);
    if(!sf||!g_gl) return 1;
    kern_return_t r;
    r=IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,mode); printf("set_id_mode->0x%x\n",r);
    Region rg; rg.n=1; rg.bounds.x=0;rg.bounds.y=0;rg.bounds.w=W;rg.bounds.h=H; rg.rect[0]=rg.bounds;
    r=IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg); printf("set_shape->0x%x\n",r);
    uint32_t dummy=0;
    r=IOConnectMethodScalarIStructureI(g_gl,0u,4,0,idx,0u,0u,0u,&dummy); printf("set_surface->0x%x\n",r);

    vm_address_t cmd=0; vm_size_t csz=0;
    if(IOConnectMapMemory(g_gl,1u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache)||!cmd){
        printf("no cmd region\n"); return 1; }
    g_hdr=(volatile uint32_t*)cmd; g_body=g_hdr+8;

    /* grid-fill VRAM 0..0x800000 step 0x10000, tag = 0xBEEF0000 | (X>>16) */
    printf("grid-filling VRAM (0..8MB step 64KB) with tags...\n");
    uint32_t X;
    for(X=0; X<0x800000u; X+=0x10000u) engine_fill(X, 0xBEEF0000u | (X>>16));

    /* surface_READ_lock (sel 12): copy_buffer_to_backing_store (VRAM buffer -> backing), then map
     * the backing for CPU reading.  So the backing now MIRRORS the surface's VRAM buffer -- if a
     * grid tag landed in that buffer it shows up here, revealing the buffer's VRAM offset. */
    uint32_t sd[32]; IOByteCount ol=sizeof sd; memset(sd,0,sizeof sd);
    kern_return_t lr=IOConnectMethodScalarIStructureO(sf,12u,0,&ol,sd);   /* surface_read_lock */
    volatile uint32_t *backing=(volatile uint32_t*)(uintptr_t)sd[0];
    uint32_t bpitch=sd[4], bh=sd[6], ndw=(bpitch*bh)/4;
    printf("read_lock kr=0x%x backing=0x%08x pitch=%u %ux%u (%u dw)\n",lr,sd[0],bpitch,sd[5],bh,ndw);
    if(!backing){printf("no backing\n");return 1;}

    /* scan backing for a tag (tags are 0xBEEF0000 | k, k = X>>16 <= 0x7f) */
    printf("scanning read-locked backing for tags (valid k<=0x7f)...\n");
    uint32_t i, found=0;
    for(i=0;i<ndw;i++){
        uint32_t v=backing[i];
        if((v&0xFFFF0000u)==0xBEEF0000u && (v&0xFFFFu)<=0x7fu){   /* real tag only */
            uint32_t k=v&0xFFFFu, Xoff=k<<16;
            long O=(long)Xoff-(long)i*4;
            printf("  HIT backing[%u]=0x%08x -> VRAM X=0x%x, surface VRAM base O=0x%lx\n",i,v,Xoff,O);
            found++; if(found>=4) break;
        }
    }
    if(!found) printf("  NO tag found in backing -- surface buffer not in the scanned range, or backing!=VRAM buffer\n");
    IOConnectMethodScalarIScalarO(sf,13u,0,0);
    IOServiceClose(g_gl); IOServiceClose(sf);
    printf("done (reboot to clear VRAM corruption)\n");
    return 0;
}
