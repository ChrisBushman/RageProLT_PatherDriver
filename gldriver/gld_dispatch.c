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

/* DISPATCH PROBE: log every primitive-slot call with its slot offset + the raw
 * PPC argument registers r4..r8, to recover the true slot->GL-primitive mapping
 * AND each slot's calling convention (the garbage `n` for fans implies the arg
 * positions differ per slot).  Draws nothing -- pure RE.  Toggle with the build
 * flag -DGLD_DISPATCH_PROBE. */
#ifdef GLD_DISPATCH_PROBE
#define MKPROBE(hx) static void dprobe_##hx(void *a,void *b,void *c,void *d,void *e,void *f) \
    { (void)a; gld_log("DISP 0x" #hx ": r4=%p r5=%p r6=%p r7=%p r8=%p\n", b,c,d,e,f); }
MKPROBE(18) MKPROBE(1c) MKPROBE(20) MKPROBE(24) MKPROBE(28) MKPROBE(2c)
MKPROBE(30) MKPROBE(34) MKPROBE(38) MKPROBE(3c) MKPROBE(40) MKPROBE(44) MKPROBE(48)
#endif

int gldUpdateDispatch(gld_context *ctx, void *table, uint32_t flags)
{
    (void)ctx; (void)flags;   /* single untextured variant for now */
    gld_log("gldUpdateDispatch(table=%p flags=0x%x) caller=%p arg5[0..2]=0x%x 0x%x 0x%x\n",
            table, flags, __builtin_return_address(0),
            ((uint32_t *)(uintptr_t)flags)[0], ((uint32_t *)(uintptr_t)flags)[1],
            ((uint32_t *)(uintptr_t)flags)[2]);

#ifdef GLD_DISPATCH_PROBE
    SET(0x18, dprobe_18); SET(0x1c, dprobe_1c); SET(0x20, dprobe_20);
    SET(0x24, dprobe_24); SET(0x28, dprobe_28); SET(0x2c, dprobe_2c);
    SET(0x30, dprobe_30); SET(0x34, dprobe_34); SET(0x38, dprobe_38);
    SET(0x3c, dprobe_3c); SET(0x40, dprobe_40); SET(0x44, dprobe_44);
    SET(0x48, dprobe_48);
#else
    /* Slot->GL-primitive mapping recovered empirically by the GLD_DISPATCH_PROBE
     * run (tools/mach64_dispatchprobe drives each GL primitive with a UNIQUE vertex
     * count; the probe logs each slot's offset + raw arg registers).  The table is
     * NOT laid out in GL-mode order, and the earlier labels were wrong:
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

    /* DrawElements vector (0x40/0x44/0x48): never fired for DrawArrays.  Reuse the
     * array bodies for now; GLEngine passes an extra index-buffer arg our 3-arg
     * funcs ignore (PPC calling convention) -> renders sequentially.  TODO: real
     * indexed path once the element width/format is recovered. */
    SET(SLOT_ELT_POINTS, mach64_render_points);
    SET(SLOT_ELT_LINES,  mach64_render_lines);
    SET(SLOT_ELT_TRIS,   mach64_render_triangles);
#endif  /* GLD_DISPATCH_PROBE */

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
