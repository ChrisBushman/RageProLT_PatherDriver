/*
 * mach64_vramscan.c - headless probe: bind a surface (the confirmed handshake), then map
 * every clientMemoryForType region on the GL + surface clients and print addr/size.  Goal:
 * find a VRAM aperture (~8MB) we can read for the Path-B render->backing bridge (Increment 2).
 * Runs over ssh (no window).  Build:
 *   cc -O2 -o /tmp/vramscan mach64_vramscan.c -framework IOKit
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>

#define GL_TYPE 1u
#define SURF_TYPE 0u
#define SEL_SET_ID_MODE 7u
#define SEL_SET_SHAPE   9u
#define SEL_SET_SURFACE 0u

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

int main(int argc,char**argv){
    uint32_t idx=(argc>1)?(uint32_t)strtoul(argv[1],0,0):0x14u, mode=0x24u;
    int16_t W=1024,H=768;   /* FULLSCREEN: does a full-scanout surface get a VRAM backing? */
    unsigned t;
    io_connect_t sf=openc(SURF_TYPE), gl=openc(GL_TYPE);
    if(!sf||!gl) return 1;
    printf("opened surface(type0)=%u GL(type1)=%u\n",(unsigned)sf,(unsigned)gl);

    kern_return_t r;
    r=IOConnectMethodScalarIScalarO(sf,SEL_SET_ID_MODE,2,0,idx,mode);
    printf("set_id_mode(0x%x,0x%x) -> 0x%08x\n",idx,mode,r);
    Region rg; rg.n=1; rg.bounds.x=0;rg.bounds.y=0;rg.bounds.w=W;rg.bounds.h=H;
    rg.rect[0]=rg.bounds;
    r=IOConnectMethodScalarIStructureI(sf,SEL_SET_SHAPE,2,sizeof(rg),0u,0u,&rg);
    printf("set_shape(%dx%d) -> 0x%08x\n",W,H,r);
    uint32_t dummy=0;
    r=IOConnectMethodScalarIStructureI(gl,SEL_SET_SURFACE,4,0,idx,0u,0u,0u,&dummy);
    printf("set_surface(0x%x) -> 0x%08x\n",idx,r);

    /* Does the kext hand back the surface's VRAM offset (a render target for the engine)? */
    { uint32_t a=0,b=0,c=0,d=0;
      kern_return_t k=IOConnectMethodScalarIScalarO(gl,5u,0,4,&a,&b,&c,&d); /* get_surface_size */
      printf("get_surface_size -> 0x%08x  [%08x %08x %08x %08x]\n",k,a,b,c,d); }
    { uint32_t a=0,b=0,c=0;
      kern_return_t k=IOConnectMethodScalarIScalarO(gl,6u,1,3,idx,&a,&b,&c); /* get_surface_info(id) */
      printf("get_surface_info(0x%x) -> 0x%08x  [%08x %08x %08x]\n",idx,k,a,b,c); }

    /* surface_write_lock (surface sel 14, ScalarIStructureO) -> IOAccelSurfaceData.  Dump the
     * FULL struct: is the fullscreen backing a VRAM address/aperture, and is there a VRAM offset
     * field the windowed surface lacked? */
    { uint32_t sd[32]; IOByteCount ol=sizeof sd; unsigned i;
      memset(sd,0,sizeof sd);
      kern_return_t k=IOConnectMethodScalarIStructureO(sf,14u,0,&ol,sd);
      printf("surface_write_lock -> 0x%08x olen=%u  backing=0x%08x pitch=%u %ux%u bpp=%u\n",
             k,(unsigned)ol,sd[0],sd[4],sd[5],sd[6],sd[7]);
      printf("  full struct:"); for(i=0;i*4<(unsigned)ol && i<24;i++) printf(" [%02x]=%08x",i*4,sd[i]);
      printf("\n");
      IOConnectMethodScalarIScalarO(sf,15u,0,0); /* write_unlock */
    }

    printf("=== clientMemoryForType on GL client ===\n");
    for(t=0;t<=7;t++){
        vm_address_t a=0; vm_size_t sz=0;
        kern_return_t mk=IOConnectMapMemory(gl,t,mach_task_self(),&a,&sz,
                                            kIOMapAnywhere|kIOMapInhibitCache);
        printf("  GL type %u -> kr=0x%08x addr=0x%08x size=0x%08x (%u KB)%s\n",
               t,mk,(unsigned)a,(unsigned)sz,(unsigned)sz/1024,
               (sz>=0x400000)?"  <== BIG (VRAM?)":"");
        if(mk==KERN_SUCCESS) IOConnectUnmapMemory(gl,t,mach_task_self(),a);
    }
    printf("=== clientMemoryForType on SURFACE client ===\n");
    for(t=0;t<=7;t++){
        vm_address_t a=0; vm_size_t sz=0;
        kern_return_t mk=IOConnectMapMemory(sf,t,mach_task_self(),&a,&sz,
                                            kIOMapAnywhere|kIOMapInhibitCache);
        printf("  SURF type %u -> kr=0x%08x addr=0x%08x size=0x%08x (%u KB)%s\n",
               t,mk,(unsigned)a,(unsigned)sz,(unsigned)sz/1024,
               (sz>=0x400000)?"  <== BIG (VRAM?)":"");
        if(mk==KERN_SUCCESS) IOConnectUnmapMemory(sf,t,mach_task_self(),a);
    }
    IOServiceClose(gl); IOServiceClose(sf);
    printf("done\n");
    return 0;
}
