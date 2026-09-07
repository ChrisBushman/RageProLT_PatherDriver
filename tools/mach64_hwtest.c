/*
 * mach64_hwtest.c - THE Path-B hardware render test, submit trigger DISCOVERED.
 * Submit = IOConnectMapMemory(gl, type 1): mapping the surface buffer runs clientMemoryForType ->
 * submit_context_buffer, which PIO-drains the type-2 command buffer through the engine.  So:
 *   fill 2D-solid-fill {reg,val} (canonical mach64 regs) into the type-2 cmd buffer, DST=surf render
 *   buffer surf[0x12c][0x28]; set count+SUBMIT; IOConnectMapMemory(type1) -> engine executes;
 *   surface_flush -> present.  (Kext has copy-skip so the render survives the flush.)
 * Console, sudo.  Build: cc -O2 -std=gnu99 -o ~/hwtest ~/hwtest.c -framework IOKit
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
static uint32_t p(uint32_t a){ uint32_t o=0; IOConnectMethodScalarIScalarO(gl,14u,1,1,a,&o); return o; }
#define ABS(x) (0x80000000u|(x))
static uint32_t bswap(uint32_t v){ return (v>>24)|((v>>8)&0xff00)|((v<<8)&0xff0000)|(v<<24); }
/* poll GUI_STAT (LE bit0 = GUI_ACTIVE) via MMIO peek until the engine is idle */
static void wait_idle(uint32_t mmio){
    for(int i=0;i<200000;i++){ uint32_t s=bswap(p(ABS(mmio+0x338))); if(!(s&1u)) return; }
    printf("  wait_idle TIMEOUT GUI_STAT=0x%08x\n",bswap(p(ABS(mmio+0x338))));
}
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    uint32_t idx=0x14u; int16_t W=1024,H=768;
    sf=openc(0u); gl=openc(1u); if(!sf||!gl) return 1;
    IOConnectMethodScalarIScalarO(sf,7u,2,0,idx,0x24u);
    Region rg={1,{0,0,W,H},{{0,0,W,H}}};
    IOConnectMethodScalarIStructureI(sf,9u,2,sizeof(rg),0u,0u,&rg);
    uint32_t dummy=0; IOConnectMethodScalarIStructureI(gl,0u,4,0,idx,0u,0u,0u,&dummy);
    uint32_t surf=p(0x10c);
    #define DIRTY(t) printf("  [%-14s] surf[0x164]=0x%08x (bit28=%d)\n",t,p(ABS(surf+0x164)),(p(ABS(surf+0x164))>>28)&1)
    DIRTY("after bind");

    /* prime: surface_write_lock + unlock + flush allocates the surface's VRAM render buffer
     * (surf[0x12c][0x28] stays 0 until then).  (surface sel 14/15/10 = write_lock/unlock/flush;
     * only the GL client's sel 14 was repurposed to peek.) */
    { uint32_t sd[32]; IOByteCount ol=sizeof sd; memset(sd,0,sizeof sd);
      IOConnectMethodScalarIStructureO(sf,14u,0,&ol,sd);
      /* fill the backing GREEN: if the final screen shows any green, the backing->buffer copy
       * is clobbering our engine RED; if clean red, the copy is skipped (engine render wins). */
      volatile uint32_t *bk=(volatile uint32_t*)(uintptr_t)sd[0];
      if(bk){ uint32_t nn=(sd[4]*sd[6])/4; for(uint32_t i=0;i<nn;i++) bk[i]=0x11111111u; } /* CPU tag */
      IOConnectMethodScalarIScalarO(sf,15u,0,0);
      IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u);
      printf("prime: backing=0x%x filled GREEN\n",sd[0]); }
    DIRTY("after prime");

    /* map the type-2 command buffer */
    vm_address_t cmd=0; vm_size_t csz=0;
    if(IOConnectMapMemory(gl,2u,mach_task_self(),&cmd,&csz,kIOMapAnywhere|kIOMapInhibitCache)||!cmd){
        printf("no cmd buffer\n"); return 1; }
    volatile uint32_t *hdr=(volatile uint32_t*)cmd, *body=hdr+8;

    /* DIRECT-SCANOUT TEST: engine can only render LOCAL VRAM; surf[0x12c] is AGP/system so a fill
     * there misses.  The scanout IS local VRAM (CRTC_OFF_PITCH byteswapped = 0x20000200, off 0x1000).
     * Fill red straight to the scanout: if the screen turns red, engine->local-VRAM render is PROVEN. */
    uint32_t desc=p(ABS(surf+0x12c)), rt=p(ABS(desc+0x28));
    /* VRAM LFB base = accel[0xe0] (getVRAMDescriptor caches map->getVirtualAddress there for fb0).
     * This is a VALIDATED mapped VA -- safe to read.  buf pixels at LFB+0x478000, scanout LFB+0x1000. */
    uint32_t eng=p(0xb0), lfb=p(ABS(eng+0xe0)), mmio=p(ABS(eng+0x550));
    printf("desc=0x%08x rt=0x%08x  accel[0xe0] LFB VA=0x%08x\n",desc,rt,lfb);
    if(lfb<0x01000000u){ printf("  LFB not cached (0x%08x) -- skipping VRAM readback\n",lfb); lfb=0; }
    if(lfb) printf("  BEFORE fill: VRAM[0x478000 buf]=0x%08x VRAM[0x1000 scanout]=0x%08x (green bswap=00ff0000)\n",
                   p(ABS(lfb+0x478000)),p(ABS(lfb+0x1000)));

    /* FLAT {reg,val} (submit_buffer PIO format), our known-working fill sequence at Apple's
     * CANONICAL mach64 offsets (base has GUI_STAT@0x338 -> low offsets).  RED full 1024x768. */
    unsigned n=0;
    #define P(r,v) do{ body[n++]=(uint32_t)(r); body[n++]=(uint32_t)(v);}while(0)
    /* ALL offsets = DRI_real - 0x400 (Apple this[0x550] = real_reg_base + 0x400). */
    P(0x014c,0);                 /* Z_CNTL (real 0x54c) */
    P(0x01fc,0);                 /* SCALE_3D_CNTL (real 0x5fc) = 0 -- disable scaler */
    P(0x0330,(1u<<0)|(1u<<1));   /* GUI_TRAJ_CNTL (real 0x730): L->R, T->B */
    P(0x02d0,0x60060666u);       /* DP_PIX_WIDTH (real 0x6d0) 32bpp */
    P(0x02c4,0x00ff0000u);       /* DP_FRGD_CLR (real 0x6c4) = RED */
    P(0x02c8,0xffffffffu);       /* DP_WRITE_MASK (real 0x6c8) */
    P(0x02d4,(3u<<0)|(7u<<16));  /* DP_MIX (real 0x6d4) */
    P(0x02d8,(1u<<0)|(1u<<8));   /* DP_SRC (real 0x6d8) */
    P(0x02a8,(0x1fffu<<16)|0);   /* SC_LEFT_RIGHT (real 0x6a8) */
    P(0x02b4,(0x1fffu<<16)|0);   /* SC_TOP_BOTTOM (real 0x6b4) */
    P(0x0308,0);                 /* CLR_CMP_CNTL (real 0x708) */
    P(0x0100,rt);                /* DST_OFF_PITCH (real 0x500) = surf buffer */
    P(0x02e8,0);                 /* DST_X_Y (real 0x6e8) = (0,0) */
    P(0x02ec,(768u<<16)|1024u);  /* DST_WIDTH_HEIGHT (real 0x6ec) = (h<<16)|w -> TRIGGER, full screen */
    #undef P
    hdr[5]=n; hdr[7]|=1u;
    printf("flat fill %u dwords (%u pairs), count=%u flags=0x%x\n",n,n/2,hdr[5],hdr[7]);

    if(mmio>0x01000000u) wait_idle(mmio);   /* engine idle BEFORE submit -> FIFO empty for our 14 writes */
    /* SUBMIT: map type 1 -> clientMemoryForType -> submit_context_buffer PIO-drains the engine */
    { vm_address_t a=0; vm_size_t s=0;
      kern_return_t k=IOConnectMapMemory(gl,1u,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache);
      printf("SUBMIT via MapMemory(type1) kr=0x%x  -> cmdbuf flags now 0x%x (0=drained)\n",k,hdr[7]);
      if(a) IOConnectUnmapMemory(gl,1u,mach_task_self(),a); }
    if(mmio>0x01000000u) wait_idle(mmio);   /* engine idle AFTER submit -> fill completed before readback */
    if(lfb) printf("  AFTER fill: VRAM[0x478000]=0x%08x  VRAM[0x1000 scanout]=0x%08x (red bswap=0000ff00)\n",
                   p(ABS(lfb+0x478000)), p(ABS(lfb+0x1000)));
    /* read back the mach64 MMIO regs (accel[0x550]=MMIO base, per submit_buffer) to see if our
     * PIO {reg,val} actually landed in the engine.  NB peek does a native (BE) load; the LE regs
     * read byte-swapped -- red 0x00ff0000 shows as 0x0000ff00. */
    { printf("  APERTURE COMPARISON (both should hold our fill values if same regs):\n");
      printf("    via MMIO this[0x550]=0x%08x:  DP_FRGD_CLR[2c4]=%08x DST_OFF_PITCH[100]=%08x DP_WRITE_MASK[2c8]=%08x DST_WH[11c]=%08x GUI_STAT[338]=%08x\n",
             mmio, p(ABS(mmio+0x2c4)),p(ABS(mmio+0x100)),p(ABS(mmio+0x2c8)),p(ABS(mmio+0x11c)),p(ABS(mmio+0x338)));
      if(lfb){ uint32_t vw=lfb+0x7ff800u;
        printf("    via VRAM LE window 0x%08x: DP_FRGD_CLR[6c4]=%08x DST_OFF_PITCH[500]=%08x DP_WRITE_MASK[6c8]=%08x DST_WH[6ec]=%08x GUI_STAT[738]=%08x\n",
               vw, p(ABS(vw+0x6c4)),p(ABS(vw+0x500)),p(ABS(vw+0x6c8)),p(ABS(vw+0x6ec)),p(ABS(vw+0x738))); } }

    /* PRESENT: engine filled the surf buffer red; surface_flush blits it to the scanout.
     * (dirty bit clear from the prime, so no backing-copy clobbers the render.) */
    kern_return_t fk=IOConnectMethodScalarIScalarO(sf,10u,2,0,1u,0u);
    printf("surface_flush -> 0x%08x\n",fk);
    printf("LOOK NOW: full-screen RED = HARDWARE engine render presented via Apple's kext!  holding 8s...\n");
    { struct timespec ts={8,0}; nanosleep(&ts,0); }
    IOConnectUnmapMemory(gl,2u,mach_task_self(),cmd);
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done.\n");
    return 0;
}
