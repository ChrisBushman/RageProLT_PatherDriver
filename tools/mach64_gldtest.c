/*
 * mach64_gldtest.c - prove the gld translation core on hardware.
 *
 * Links the REAL plugin translation code (gldriver/gld_render.c + gld_state.c +
 * common/mach64_emit.c) and drives it end to end, bypassing only Apple's
 * GLEngine (which we can't call without the private dispatch ABI).  We build a
 * gld_context by hand -- open the connection, map the type-4 command buffer +
 * the VRAM surface, allocate/init the backend -- then call the actual gld* path:
 *
 *   mach64_init_state -> mach64_set_render_target -> mach64_pack_vertex x3
 *   -> mach64_draw_triangle -> gldFinish(wait_for_stamp)
 *
 * mach64_draw_triangle runs the full chain: m64_emit_state + m64_emit_triangle
 * (reciprocal-area trigger) -> m64_cb_write_le into the command buffer ->
 * new_vertex_buffer -> the kext drives the ring.  If the gouraud triangle shows
 * up in the off-screen surface, the translation core is proven on hardware.
 *
 * Build: links the gld sources (see tools/Makefile).  -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include "../gldriver/gld.h"
#include "../common/mach64_abi.h"

#define PITCH_PIX   128
#define ROW_BYTES   (PITCH_PIX*4)
#define TARGET_OFF  0x600000u

static unsigned SURF;
static unsigned pix(int x,int y){ return OSReadLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4); }

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t cmd=0, surf=0; vm_size_t cs=0, ss=0;
    gld_context *ctx; m64_vertex v0,v1,v2; int x,y;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_CMDBUF,mach_task_self(),&cmd,&cs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map cmdbuf\n");return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&surf,&ss,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map surface\n");return 1;}
    SURF=surf;

    /* hand-build a gld_context (bypass gldCreateContext / GLEngine) */
    ctx=calloc(1,0x200);
    ctx->connect = conn;
    ctx->fb      = (void*)cmd;                 /* type-4 command buffer */
    ctx->backend = calloc(1,sizeof(mach64_priv));
    if(!ctx->backend){fprintf(stderr,"no backend\n");return 1;}

    /* drive the REAL translation entry points */
    mach64_init_state((mach64_priv*)ctx->backend);         /* gouraud defaults */
    mach64_set_render_target(ctx, TARGET_OFF, PITCH_PIX);  /* -> DST_OFF_PITCH */

    /* clear the off-screen target */
    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);

    mach64_pack_vertex(&v0,  20.0f,  16.0f, 0.0f, 1.0f, 0xFFFF0000u); /* red   */
    mach64_pack_vertex(&v1, 110.0f,  44.0f, 0.0f, 1.0f, 0xFF00FF00u); /* green */
    mach64_pack_vertex(&v2,  50.0f, 116.0f, 0.0f, 1.0f, 0xFF0000FFu); /* blue  */

    printf("gld translation core -> mach64_draw_triangle -> kext ring\n\n");
    if(!mach64_draw_triangle(ctx, &v0, &v1, &v2))
        printf("WARN: mach64_draw_triangle returned 0 (degenerate)\n");
    gldFinish(ctx);                                        /* wait_for_stamp */

    /* readback: gouraud triangle present == the gld emit path drove the kext */
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
            ? "GLD CORE PROVEN: gld_render/gld_state/mach64_emit drove the kext ring!"
            : (nfill>50 ? "shape drawn but single color -- translation/state issue"
                        : "triangle not present -- gld path did not drive the ring"));
    }
    gldFinish(ctx);
    IOServiceClose(conn); return 0;
}
