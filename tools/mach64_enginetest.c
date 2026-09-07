/*
 * mach64_enginetest.c - THE definitive Path-B hardware test: can the mach64 2D engine render
 * into a presentable VRAM region via APPLE's command buffer (submit_buffer), with NO kext of ours?
 *
 * Binds a fullscreen surface, maps the type-1 command region (VendorCommandBufferHeader), writes
 * a mach64 2D SOLID-FILL register stream as {reg,val} pairs (red 256x256 at VRAM offset 0), sets
 * count+SUBMIT, calls finish (sel 8) to PIO-drain it, then (a) reads the surface backing to see if
 * VRAM offset 0 == the backing, and (b) surface_flush to present.  If a red square appears on
 * screen (top-left) OR the backing shows red, the engine renders via Apple's kext -> HW Path-B viable.
 *
 * RISK: a 2D fill to VRAM offset 0 -- if that's the live scanout, a red square flashes top-left
 * (WindowServer repaints); no CRTC write, so no page-flip race.  Run AT THE CONSOLE:  sudo ./enginetest
 * Build: cc -O2 -std=gnu99 -o /tmp/enginetest mach64_enginetest.c -framework IOKit
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define GL_TYPE 1u
#define SURF_TYPE 0u
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
    io_connect_t sf=openc(SURF_TYPE), gl=openc(GL_TYPE);
    if(!sf||!gl) return 1;
    kern_return_t r;
    r=IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,mode); printf("set_id_mode -> 0x%08x\n",r);
    Region rg; rg.n=1; rg.bounds.x=0;rg.bounds.y=0;rg.bounds.w=W;rg.bounds.h=H; rg.rect[0]=rg.bounds;
    r=IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg); printf("set_shape -> 0x%08x\n",r);
    uint32_t dummy=0;
    r=IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy); printf("set_surface -> 0x%08x\n",r);

    /* backing ptr (write_lock) so we can read whether the engine fill lands in it */
    uint32_t sd[32]; IOByteCount ol=sizeof sd; memset(sd,0,sizeof sd);
    IOConnectMethodScalarIStructureO(sf,14u,0,&ol,sd);
    volatile uint32_t *backing=(volatile uint32_t*)(uintptr_t)sd[0];
    printf("backing=0x%08x pitch=%u %ux%u  before: [0]=%08x [1]=%08x\n",
           sd[0],sd[4],sd[5],sd[6], backing?backing[0]:0, backing?backing[1]:0);

    /* map the type-1 command region (VendorCommandBufferHeader + body) */
    vm_address_t cmd=0; vm_size_t csz=0;
    kern_return_t mk=IOConnectMapMemory(gl,1u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache);
    printf("cmd region kr=0x%08x addr=0x%08x size=0x%08x\n",mk,(unsigned)cmd,(unsigned)csz);
    if(mk||!cmd){ printf("no command region\n"); return 1; }
    volatile uint32_t *hdr=(volatile uint32_t*)cmd;
    volatile uint32_t *body=hdr+8;             /* +0x20 */
    printf("hdr before: cap=%u count=%u stamp=%u flags=0x%x body[0]=%08x\n",
           hdr[4],hdr[5],hdr[6],hdr[7],body[0]);

    /* mach64 2D solid-fill: red 256x256 at VRAM offset 0, pitch 1024 px.  {reg(byte),val} pairs. */
    unsigned n=0;
    #define P(reg,val) do{ body[n++]=(uint32_t)(reg); body[n++]=(uint32_t)(val); }while(0)
    P(0x054c,0);                 /* Z_CNTL off              */
    P(0x05fc,0);                 /* SCALE_3D_CNTL off       */
    P(0x0730,3);                 /* GUI_TRAJ_CNTL L->R T->B */
    P(0x06d0,0x60060666u);       /* DP_PIX_WIDTH 32bpp      */
    P(0x06c4,0x00ff0000u);       /* DP_FRGD_CLR = RED       */
    P(0x06c8,0xffffffffu);       /* DP_WRITE_MASK           */
    P(0x06d4,0x00070003u);       /* DP_MIX BKGD_D|FRGD_S    */
    P(0x06d8,0x00000101u);       /* DP_SRC both = FRGD_CLR  */
    P(0x06a8,0x1fff0000u);       /* SC_LEFT_RIGHT           */
    P(0x06b4,0x1fff0000u);       /* SC_TOP_BOTTOM           */
    P(0x0708,0);                 /* CLR_CMP_CNTL            */
    P(0x0500,(128u<<22)|0u);     /* DST_OFF_PITCH: pitch 1024px, off 0 */
    P(0x06e8,0);                 /* DST_X_Y (0,0)           */
    P(0x06ec,(256u<<16)|256u);   /* DST_WIDTH_HEIGHT trigger fill */
    hdr[5]=n;                    /* count (+0x14) */
    hdr[7]|=1u;                  /* flags bit0 = SUBMIT (+0x1c) */
    printf("submitting %u dwords (%u pairs) ...\n",n,n/2);
    r=IOConnectMethodScalarIStructureI(gl,8u,0,0); /* finish -> drain + idle */
    printf("finish -> 0x%08x  hdr after: count=%u flags=0x%x stamp=%u\n",r,hdr[5],hdr[7],hdr[6]);

    printf("backing after fill: [0]=%08x [1]=%08x  (red=0x00ff0000 => VRAM0==backing)\n",
           backing?backing[0]:0, backing?backing[1]:0);
    r=IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u); printf("surface_flush -> 0x%08x\n",r);

    IOConnectMethodScalarIScalarO(sf,15u,0,0);
    IOConnectUnmapMemory(gl,1u,mach_task_self(),cmd);
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done -- LOOK AT THE SCREEN top-left for a RED square\n");
    return 0;
}
