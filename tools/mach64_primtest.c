/*
 * mach64_primtest.c - prove the render-slot bodies + gldUpdateDispatch on
 * hardware, driven THROUGH the dispatch table the way GLEngine would.
 *
 * Steps (no GLEngine, but its exact calling path):
 *   1. hand-build a gld_context (open conn, map cmdbuf+surface, init state).
 *   2. build a synthetic incoming vertex array: 3 gli vertices at stride 0x100,
 *      each with X_Y/ARGB/Z pre-packed at the recovered offsets (R/G/B tri).
 *   3. call gldUpdateDispatch(ctx, table, 0) to install our render funcs.
 *   4. fetch the TRIANGLES slot (table+0x28) and CALL THROUGH IT: f(ctx,verts,3).
 *      -> mach64_render_triangles -> mach64_read_vertex -> mach64_draw_triangle
 *      -> m64_emit_* -> new_vertex_buffer -> kext ring.
 *   5. gldFinish, read back the surface, verify the gouraud triangle.
 *
 * Proves the whole userspace stack from the dispatch table down to pixels.
 * Build: links the gld sources (see tools/Makefile).  -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gldriver/gld.h"
#include "../common/mach64_abi.h"

#define PITCH_PIX   128
#define ROW_BYTES   (PITCH_PIX*4)
#define TARGET_OFF  0x600000u

static unsigned SURF;
static unsigned pix(int x,int y){ return OSReadLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4); }

/* write one synthetic incoming gli vertex (host order; the render body reads it
 * host-order and m64_cb_write_le byte-swaps to LE, exactly like the real path) */
static void put_vertex(unsigned char *v, int x, int y, unsigned argb)
{
    int xy = ((x*4)<<16) | ((y*4)&0xffff);
    float w = 1.0f;
    memset(v, 0, MACH64_GLIVERTEX_STRIDE);
    *(int      *)(v + 0x30) = xy;    /* X_Y  (pre-packed) */
    *(unsigned *)(v + 0x34) = argb;  /* ARGB (pre-packed) */
    *(int      *)(v + 0x38) = 0;     /* Z                 */
    memcpy(v + 0x08, &w, 4);         /* W = 1.0f          */
}

int main(void)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr;
    vm_address_t cmd=0, surf=0; vm_size_t cs=0, ss=0;
    gld_context *ctx; unsigned char *varray; int x,y;
    unsigned char table[0x80];
    gld_render_fn render_tris;

    svc=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("Mach64Accelerator"));
    if(!svc){fprintf(stderr,"no service\n");return 1;}
    kr=IOServiceOpen(svc,mach_task_self(),MACH64_GL_CONNECT_TYPE,&conn); IOObjectRelease(svc);
    if(kr){fprintf(stderr,"open 0x%x\n",kr);return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_CMDBUF,mach_task_self(),&cmd,&cs,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map cmdbuf\n");return 1;}
    if(IOConnectMapMemory(conn,MACH64_MEM_SURFACE,mach_task_self(),&surf,&ss,kIOMapAnywhere|kIOMapInhibitCache)){fprintf(stderr,"map surface\n");return 1;}
    SURF=surf;

    ctx=calloc(1,0x200);
    ctx->connect=conn; ctx->fb=(void*)cmd;
    ctx->backend=calloc(1,sizeof(mach64_priv));
    mach64_init_state((mach64_priv*)ctx->backend);
    mach64_set_render_target(ctx, TARGET_OFF, PITCH_PIX);

    for(y=0;y<128;y++) for(x=0;x<128;x++) OSWriteLittleInt32((void*)SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);

    /* synthetic 3-vertex incoming array at stride 0x100 */
    varray=calloc(3, MACH64_GLIVERTEX_STRIDE);
    put_vertex(varray + 0*MACH64_GLIVERTEX_STRIDE,  20,  16, 0xFFFF0000u); /* red   */
    put_vertex(varray + 1*MACH64_GLIVERTEX_STRIDE, 110,  44, 0xFF00FF00u); /* green */
    put_vertex(varray + 2*MACH64_GLIVERTEX_STRIDE,  50, 116, 0xFF0000FFu); /* blue  */

    /* install our render funcs into a dispatch table + verify the slots */
    memset(table,0,sizeof table);
    gldUpdateDispatch(ctx, table, 0);
    printf("gldUpdateDispatch: TRIANGLES slot(0x28)=%p (want %p), FINISH(0x58)=%p\n",
           *(void**)(table+0x28), (void*)mach64_render_triangles, *(void**)(table+0x58));

    /* CALL THROUGH THE TABLE, as GLEngine would */
    render_tris = *(gld_render_fn*)(table+0x28);
    printf("\ncalling table[0x28](ctx, verts, 3) -> render body -> kext ring\n\n");
    render_tris(ctx, varray, 3);
    gldFinish(ctx);

    {
        int nfill=0,nR=0,nG=0,nB=0,classes;
        printf("off-screen readback (R/G/B = dominant channel):\n");
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
        printf("\nsamples: v0=%08x v1=%08x v2=%08x cen=%08x  covered=%d R=%d G=%d B=%d\n",
               pix(26,30),pix(96,50),pix(52,102),pix(58,58),nfill,nR,nG,nB);
        classes=(nR>0)+(nG>0)+(nB>0);
        printf("\n%s\n", (nfill>50 && classes>=2)
            ? "RENDER SLOTS PROVEN: gldUpdateDispatch + render body drew via the ring!"
            : (nfill>50 ? "shape drawn but single color" : "triangle not present"));
    }
    IOServiceClose(conn); return 0;
}
