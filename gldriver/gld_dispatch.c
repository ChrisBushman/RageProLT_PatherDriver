/*
 * gld_dispatch.c - gldUpdateDispatch: install our render-slot bodies into the
 * dispatch table GLEngine hands us.
 *
 * Slot offsets recovered from ATIRage128GLDriver`gldUpdateDispatch (see
 * docs/re-notes/gld-layer.md): the render funcs live at table+0x18..0x48, the
 * control funcs (Finish/Flush) at 0x58/0x5c.  Apple selects a per-texture-format
 * variant from ctx+0x2c; we support only the untextured fixed-function path, so
 * we install one render set.
 */
#include "gld.h"

/* dispatch-table slot byte offsets (recovered) */
#define SLOT_POINTS      0x18
#define SLOT_LINES       0x1c
#define SLOT_LINE_LOOP   0x20   /* strip/loop assignment inferred */
#define SLOT_LINE_STRIP  0x24
#define SLOT_TRIANGLES   0x28
#define SLOT_TRI_STRIP   0x2c
#define SLOT_TRI_FAN     0x30
#define SLOT_POLYGON     0x34
#define SLOT_QUADS       0x38
#define SLOT_QUAD_STRIP  0x3c
#define SLOT_ELT_POINTS  0x40   /* 2nd (DrawElements) vector */
#define SLOT_ELT_LINES   0x44
#define SLOT_ELT_TRIS    0x48
#define SLOT_COMPLETE    0x4c
#define SLOT_FINISH      0x58
#define SLOT_FLUSH       0x5c

#define SET(off, fn)  (*(void **)((unsigned char *)table + (off)) = (void *)(fn))

int gldUpdateDispatch(gld_context *ctx, void *table, uint32_t flags)
{
    (void)ctx; (void)flags;   /* single untextured variant for now */
    gld_log("gldUpdateDispatch(table=%p flags=0x%x) caller=%p arg5[0..2]=0x%x 0x%x 0x%x\n",
            table, flags, __builtin_return_address(0),
            ((uint32_t *)(uintptr_t)flags)[0], ((uint32_t *)(uintptr_t)flags)[1],
            ((uint32_t *)(uintptr_t)flags)[2]);

    SET(SLOT_POINTS,     mach64_render_points);
    SET(SLOT_LINES,      mach64_render_lines);
    SET(SLOT_LINE_LOOP,  mach64_render_line_loop);
    SET(SLOT_LINE_STRIP, mach64_render_line_strip);
    /* EMPIRICAL: GLEngine dispatches glDrawArrays(GL_TRIANGLES) to slot 0x2c (what
     * we labeled TRI_STRIP), delivering INDEPENDENT triangles (6 verts = 2 tris);
     * slot 0x28 is never called for array triangles.  Installing the STRIP
     * decomposer at 0x2c mis-built every quad -- it treated the 2 independent
     * triangles as a strip, producing a redundant overlapping triangle whose
     * differing interpolation showed as a diagonal seam.  Install the correct
     * independent-triangle body at 0x2c.  (0x2c is really GL_TRIANGLES; the
     * remaining triangle-region labels are likely off by one, but only the
     * triangles path is exercised/confirmed -- leave the rest until tested.) */
    SET(SLOT_TRIANGLES,  mach64_render_triangles);
    SET(SLOT_TRI_STRIP,  mach64_render_triangles);
    SET(SLOT_TRI_FAN,    mach64_render_tri_fan);
    SET(SLOT_POLYGON,    mach64_render_polygon);
    SET(SLOT_QUADS,      mach64_render_quads);
    SET(SLOT_QUAD_STRIP, mach64_render_quad_strip);

    /* DrawElements vector: reuse the array bodies for now.  GLEngine passes an
     * extra index-buffer arg in r6 which our 3-arg funcs safely ignore (PPC
     * calling convention) -> renders sequentially.  TODO: real indexed path
     * once the element width/format is recovered. */
    SET(SLOT_ELT_POINTS, mach64_render_points);
    SET(SLOT_ELT_LINES,  mach64_render_lines);
    SET(SLOT_ELT_TRIS,   mach64_render_triangles);

    /* control slots (0x58/0x5c symbol-confirmed == gldFinish/gldFlush) */
    SET(SLOT_FINISH,     gldFinish);
    SET(SLOT_FLUSH,      gldFlush);
    /* 0x4c (buffer-complete) / 0x60 (render-control): the reference installs
     * these; install logging bodies to see if GLEngine calls them to flush. */
    SET(SLOT_COMPLETE,   mach64_dispatch_complete);
    SET(0x60,            mach64_dispatch_render_ctl);

    gld_log("  slot[0x28]=%p slot[0x48]=%p installed at table+off\n",
            (void *)mach64_render_triangles, (void *)mach64_render_triangles);

    /* MUST return 0: GLEngine compares this return (r3) to a cached flag to
     * decide whether to keep our hardware dispatch or reconfigure to software
     * (GLEngine 0x46d4).  A void return left garbage in r3 -> not-equal ->
     * software reinstall over our slots every draw.  Reference returns 0. */
    return 0;
}
