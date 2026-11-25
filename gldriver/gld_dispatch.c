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

    /* Slot->GL-primitive mapping, recovered empirically.  The table is
     * NOT laid out in GL-mode order:
     *   0x18 POINTS  0x1c LINES  0x20 LINE_STRIP(+LINE_LOOP)  0x28 POLYGON
     *   0x2c TRIANGLES  0x30 TRIANGLE_FAN  0x34 TRIANGLE_STRIP
     *   0x38 QUADS  0x3c QUAD_STRIP        (0x24 never fired for DrawArrays)
     * All slots pass f(ctx, verts, count) EXCEPT the fan (0x30), which passes
     * f(ctx, apex, perimeter, count) -- see mach64_render_tri_fan_slot. */
    SET(0x18, mach64_render_points);
    SET(0x1c, mach64_render_lines);
    SET(0x20, mach64_render_line_strip);   /* LINE_STRIP and LINE_LOOP both land here;
                                            * loop arrives with its closing vertex
                                            * appended (count = n+1), so the plain
                                            * strip body draws the closing segment. */
    SET(0x24, mach64_render_line_strip);   /* not observed for DrawArrays; safe body */
    SET(0x28, mach64_render_polygon);      /* GL_POLYGON (convex -> fan) */
    SET(0x2c, mach64_render_triangles);    /* GL_TRIANGLES (independent) */
    SET(0x30, mach64_render_tri_fan_slot); /* GL_TRIANGLE_FAN (apex/perimeter/count) */
    SET(0x34, mach64_render_tri_strip);    /* GL_TRIANGLE_STRIP */
    SET(0x38, mach64_render_quads);        /* GL_QUADS */
    SET(0x3c, mach64_render_quad_strip);   /* GL_QUAD_STRIP */

    /* Elements vectors (0x40/0x44/0x48).  Plain glDrawElements is flattened by GLEngine back
     * into the DrawArrays slots above (verified via tools/mach64_elementsprobe), so those
     * never fire for simple indexed draws.  BUT these slots ARE used for software-CLIPPED
     * geometry: when GLEngine clips a primitive (GL_SCISSOR_TEST, or a frustum crossing) it
     * re-dispatches the survivors here as an ARRAY OF VERTEX POINTERS -- a different format
     * than the flat DrawArrays slots -- handled by mach64_render_elt_points/lines/tris
     * (docs/re-notes/scissor.md, tools/mach64_clipptlntest). */
    SET(SLOT_ELT_POINTS, mach64_render_elt_points); /* clipped points (pointer array) */
    SET(SLOT_ELT_LINES,  mach64_render_elt_lines);  /* clipped lines  (pointer-pair array) */
    SET(SLOT_ELT_TRIS,   mach64_render_elt_tris);   /* clipped convex polygons (pointer array) */

    /* control slots (0x58/0x5c symbol-confirmed == gldFinish/gldFlush) */
    SET(SLOT_FINISH,     gldFinish);
    SET(SLOT_FLUSH,      gldFlush);
    /* 0x4c (buffer-complete) / 0x60 (render-control): the reference installs
     * these. */
    SET(SLOT_COMPLETE,   mach64_dispatch_complete);
    SET(0x60,            mach64_dispatch_render_ctl);
    SET(0x64,            mach64_signal_fence);      /* gld_signalFence(ctx, fence) in the reference */
    SET(0x68,            mach64_begin_query);       /* gld_beginQuerySlot(ctx, query) */
    SET(0x6c,            mach64_end_query);

    /* DOUBLE-BUFFER PRESENT lives in mach64_dispatch_render_ctl (slot 0x60), NOT here.
     * gldUpdateDispatch fires per state-change (~29-48x/frame in a real app), so presenting
     * here page-flipped + waited vblank dozens of times a frame -- atlantis crawled at <1fps.
     * Slot 0x60 is GLEngine's once-per-frame swap hook; the present cycle runs there now. */

    /* MUST return 0: GLEngine compares this return (r3) to a cached flag to
     * decide whether to keep our hardware dispatch or reconfigure to software
     * (GLEngine 0x46d4).  A void return left garbage in r3 -> not-equal ->
     * software reinstall over our slots every draw.  Reference returns 0. */
    return 0;
}
