/*
 * mach64_ringtest.c - drive the KEXT's DMA ring end to end.
 *
 * Unlike mach64_gouraud (which orchestrated the bus master itself via MMIO),
 * this tool touches NO registers.  It only:
 *   1. maps the type-4 command buffer + the VRAM surface,
 *   2. fills the command buffer with a COMPLETE register-write stream
 *      (3D state + 3 gouraud vertices + ONE_OVER_AREA_UC trigger + SRC_CNTL=0
 *      terminator),
 *   3. calls new_vertex_buffer(size) -> the kext's rotateCommandBuffer(), which
 *      builds the ring descriptor, quiesces, activates bus-mastering, kicks, and
 *      waits for idle,
 *   4. calls wait_for_stamp(stamp),
 *   5. reads back the off-screen triangle to prove the kext drove the ring.
 *
 * So this validates the whole kernel submission path (rotateCommandBuffer /
 * submitCommandBuffer / waitForStamp) that the GLDriver bundle will use.
 *
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <string.h>
#include <stdio.h>
#include "../common/mach64_abi.h"
#include "../common/mach64_regs.h"

#define PITCH_PIX    128
#define ROW_BYTES    (PITCH_PIX*4)
#define TARGET_OFF   0x600000u
#define ONE_F        0x3f800000u          /* 1.0f */

static unsigned FB, CMD, put;             /* CMD = cmd-buffer VA, put = offset */

/* append a register-write packet to the command buffer (little-endian).
 * header = ADRINDEX(reg) | ((n_regs-1)<<16), then n_regs values. */
static void emit1(unsigned reg, unsigned val)
{
    OSWriteLittleInt32((void*)CMD, put, MACH64_ADRINDEX(reg)); put+=4;
    OSWriteLittleInt32((void*)CMD, put, val);                  put+=4;
}
static void emitN(unsigned reg, const unsigned *vals, int n)
{
    OSWriteLittleInt32((void*)CMD, put, MACH64_ADRINDEX(reg) | ((unsigned)(n-1)<<16)); put+=4;
    { int i; for(i=0;i<n;i++){ OSWriteLittleInt32((void*)CMD, put, vals[i]); put+=4; } }
}

static unsigned mapmem(io_connect_t c, unsigned type)
{
    vm_address_t a=0; vm_size_t s=0;
    if (IOConnectMapMemory(c,type,mach_task_self(),&a,&s,kIOMapAnywhere|kIOMapInhibitCache)!=KERN_SUCCESS)
        return 0;
    return a;
}

static const int TX[3]={20,110,50}, TY[3]={16,44,116};
static const unsigned VC[3]={0xFFFF0000u,0xFF00FF00u,0xFF0000FFu};  /* R,G,B */
static unsigned pix(int x,int y){ return OSReadLittleInt32((void*)FB, TARGET_OFF + y*ROW_BYTES + x*4); }

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    int i,x,y,xx[3],yy[3]; double area; float ooa; unsigned oob;
    unsigned bufferID=0, stamp; kern_return_t r_new, r_wait;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    FB =mapmem(conn,MACH64_MEM_SURFACE);
    CMD=mapmem(conn,MACH64_MEM_CMDBUF);
    if(!FB||!CMD){fprintf(stderr,"map surface/cmdbuf failed\n");goto out;}

    /* clear the off-screen target */
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)FB, TARGET_OFF + y*ROW_BYTES + x*4, 0);

    for(i=0;i<3;i++){ xx[i]=TX[i]*4; yy[i]=TY[i]*4; }
    area=(1.0/16.0)*((double)(xx[0]-xx[2])*(yy[1]-yy[2])-(double)(yy[0]-yy[2])*(xx[1]-xx[2]));
    ooa=(float)(1.0/area); memcpy(&oob,&ooa,4);

    /* ---- build the command buffer: everything the draw needs, as a stream ---- */
    put = 0;
    /* static 3D state (gouraud: color from the interpolating 3D scaler) */
    emit1(MACH64_SCALE_3D_CNTL, MACH64_SCALE_3D_CNTL_SOLID);   /* 0x100c1 */
    emit1(MACH64_SETUP_CNTL,    MACH64_SETUP_CNTL_SHADE);      /* 0       */
    emit1(MACH64_Z_CNTL,        0);
    emit1(MACH64_DP_PIX_WIDTH,  0x60060666);                  /* ARGB8888 */
    emit1(MACH64_DST_OFF_PITCH, ((PITCH_PIX/8)<<22)|(TARGET_OFF>>3));
    emit1(MACH64_SC_LEFT_RIGHT, 0x1fff0000);
    emit1(MACH64_SC_TOP_BOTTOM, 0x3fff0000);
    emit1(MACH64_DP_WRITE_MASK, 0xffffffff);
    emit1(MACH64_CLR_CMP_CNTL,  0);
    emit1(MACH64_DP_MIX,        MACH64_DP_MIX_SS);             /* 0x00070007 */
    emit1(MACH64_DP_SRC,        MACH64_DP_SRC_3D);             /* 0x0505     */
    /* three full vertices incl W=1.0 (perspective) */
    for(i=0;i<3;i++){
        unsigned v[7] = { 0,0,ONE_F,0, 0, VC[i], (unsigned)((xx[i]<<16)|(yy[i]&0xffff)) };
        emitN(MACH64_VERTEX_SLOT(i), v, 7);   /* S,T,W,SPEC,Z,ARGB,X_Y */
    }
    emit1(MACH64_ONE_OVER_AREA_UC, oob);      /* trigger the draw */
    emit1(MACH64_SRC_CNTL, 0);                /* stream terminator */

    printf("mach64_ringtest: %u-byte gouraud stream in the command buffer\n", put);
    printf("calling new_vertex_buffer -> kext rotateCommandBuffer (drives the ring)\n\n");

    /* ---- hand it to the kext: this kicks the DMA ring ---- */
    r_new = IOConnectMethodScalarIScalarO(conn, kMach64Sel_new_vertex_buffer, 1, 1,
                                          (int)put, &bufferID);
    stamp = bufferID;
    printf("new_vertex_buffer -> kr=0x%x stamp=%u\n", r_new, stamp);
    r_wait = IOConnectMethodScalarIStructureI(conn, kMach64Sel_wait_for_stamp, 1, 0,
                                              (int)stamp, NULL);
    printf("wait_for_stamp(%u) -> kr=0x%x\n\n", stamp, r_wait);

    /* ---- read back: kext drove the ring if the gouraud triangle is here ---- */
    {
        int nfill=0,nR=0,nG=0,nB=0,classes;
        printf("off-screen readback (R/G/B = dominant channel, '.' = empty):\n");
        for(y=0;y<124;y+=4){
            printf("  ");
            for(x=0;x<124;x+=2){
                unsigned v=pix(x,y);
                if(!v){ putchar('.'); continue; }
                unsigned rr=(v>>16)&0xff, g=(v>>8)&0xff, b=v&0xff; nfill++;
                if(rr>=g && rr>=b){ putchar('R'); nR++; }
                else if(g>=rr && g>=b){ putchar('G'); nG++; }
                else { putchar('B'); nB++; }
            }
            putchar('\n');
        }
        printf("\nsamples: near-v0=%08x near-v1=%08x near-v2=%08x centroid=%08x\n",
               pix(26,30), pix(96,50), pix(52,102), pix(58,58));
        printf("covered=%d  dominant R=%d G=%d B=%d\n", nfill, nR,nG,nB);
        classes=(nR>0)+(nG>0)+(nB>0);
        printf("\n%s\n", (nfill>50 && classes>=2)
            ? "KEXT RING WORKS: kernel rotateCommandBuffer drove the DMA gouraud draw!"
            : (nfill>50 ? "shape drawn but single color -- stream/state issue"
                        : "triangle not present -- kext did not drive the ring"));
    }
out:
    IOServiceClose(conn); return 0;
}
