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

    /* DOUBLE-BUFFER PRESENT.  A DB context gets NO per-frame flush/finish/swap call
     * from GLEngine -- gldFlush/gldFinish/gldFlushBuffer never fire (the reference
     * relies on the kernel/WindowServer to present; our minimal kext can't).  What
     * GLEngine DOES do is re-invoke gldUpdateDispatch once per frame, so drive the
     * present cycle here: submit the just-finished frame's batched triangles to the
     * back buffer, blit back->front, and arm a back-clear for the next frame.  The
     * batch holds the previous frame's geometry (gldUpdateDispatch is the frame
     * boundary), so this presents with one frame of latency but keeps the swap chain
     * running with no app-side glFinish.  Guard: no-op until a DB drawable is bound. */
    {   mach64_priv *p = MACH64_PRIV(ctx);
        if (p && p->dbuf && p->back_off) {
            /* gldUpdateDispatch fires at FRAME END (after this frame's render slots),
             * so finalize the frame right here with NO latency: render the batched
             * triangles to the back, present, then clear the back for the next frame.
             * We own the clear explicitly (frame_new=0) rather than deferring it to the
             * next draw -- the deferred path was leaving the back uncleared -> residue. */
            /* PAGE-FLIP present (register-guide-derived, replaces the blit): render
             * this frame to the hidden buffer (render_off), flip the CRTC scanout to
             * it (loads at vblank -- tear-free, zero-copy), then swap roles and clear
             * the newly-hidden buffer for the next frame.  Alternates front<->back. */
            /* DEFERRED-WAIT present: wait for the PREVIOUS frame's flip to retire HERE --
             * at the frame boundary, after GLEngine has already dispatched this frame's
             * geometry -- so the ~vblank wait overlaps that dispatch instead of stalling
             * inside the flip.  Then render_off (freed by that flip) is safe to draw. */
            mach64_wait_flip(ctx);                /* wait prev flip (overlapped w/ dispatch) */
            mach64_flush_batch(ctx);              /* this frame: clear+geometry, 1 DMA -> render_off */
            mach64_flip(ctx, p->render_off);      /* write CRTC, record pending (no wait) */
            p->render_off = (p->render_off == p->front_off) ? p->back_off
                                                            : p->front_off;
            mach64_set_render_target(ctx, p->render_off, p->fb_pitch);
            /* PERF: fold the new frame's clears into the batch so clear + geometry go out
             * as ONE submit.  Z clear self-gates on Z_EN (its own submit only when depth
             * is on); the colour clear is emitted straight into the batch, ahead of this
             * frame's triangles. */
            /* Keep the Z clear as its OWN submit (not folded into the batch): it overlaps
             * the pending page-flip / vblank dead time and is effectively free there --
             * folding it into the geometry submit serialised it into the critical path and
             * measured ~7ms/frame SLOWER (HW-timed).  So the separate submit wins. */
            if (p->z_off) mach64_clear_zbuffer(ctx);
            mach64_batch_clear_color(ctx);        /* clear folded into the triangle batch */
            p->frame_new = 0;
        }
    }

    /* MUST return 0: GLEngine compares this return (r3) to a cached flag to
     * decide whether to keep our hardware dispatch or reconfigure to software
     * (GLEngine 0x46d4).  A void return left garbage in r3 -> not-equal ->
     * software reinstall over our slots every draw.  Reference returns 0. */
    return 0;
}
