/* gld_cgs.c - resolve the windowed drawable's on-screen content bounds via the private CoreGraphics
 * CGS API (userspace, NO kext), for the windowed render+blit path.  GLEngine hands our bundle only
 * a compact drawable descriptor; the actual window geometry lives in CGS.
 *
 * Validated resolver (mach64_wintest/mach64_multiwin, Lombard): combine the window's screen FRAME rect
 * with the surface's WINDOW-RELATIVE bounds:
 *     screen_content.origin = CGSGetScreenRectForWindow(win).origin + CGSGetSurfaceBounds(surf).origin
 *     screen_content.size   = surface bounds size
 * e.g. window frame (120,58 400x322) + surface (0,22 400x300) = content (120,80 400x300).
 *
 * KEY (mach64_multiwin): the descriptor's d[1] is the CGS WINDOW id and d[2] the CGS SURFACE id (they
 * matched CGS exactly for two distinct windows), so we resolve the EXACT window+surface -- correct for
 * ANY number of GL windows.  (d[0] is the same across windows; the old d[0]-heuristic only worked by
 * z-order luck.)  Replaces the MACH64_WIN_RECT env injection; returns 0 (caller falls back to the env
 * var / fullscreen path) if nothing resolves.  Pure CoreGraphics -> safe, no panic path. */
#include "gld.h"
#include <stdint.h>
#include <ApplicationServices/ApplicationServices.h>   /* CGRect */

/* Private CGS API (Panther), signatures reversed from CoreGraphics. */
typedef int CGSConnectionID; typedef int CGSWindowID; typedef int CGSSurfaceID;
extern CGSConnectionID _CGSDefaultConnection(void);
extern int CGSGetWindowList(CGSConnectionID cid, CGSConnectionID target, int cap, CGSWindowID *list, int *cnt);
extern int CGSGetSurfaceList(CGSConnectionID cid, CGSWindowID wid, int cap, CGSSurfaceID *list, int *cnt);
extern int CGSGetScreenRectForWindow(CGSConnectionID cid, CGSWindowID wid, CGRect *r);
extern int CGSGetSurfaceBounds(CGSConnectionID cid, CGSWindowID wid, CGSSurfaceID sid, CGRect *b);

int mach64_cgs_resolve_bounds(uint32_t window_id, uint32_t surface_id,
                              int *ox, int *oy, int *ow, int *oh)
{
    CGSConnectionID cid = _CGSDefaultConnection();
    CGSWindowID wins[256]; int nwin = 0, i, j;
    if (!cid) return 0;

    /* DIRECT, multi-window correct: the windowed drawable descriptor's d[1] is the CGS WINDOW id and
     * d[2] the CGS SURFACE id -- HW-probed (tools/mach64_multiwin): for two windows d[1]/d[2] matched
     * the window/surface CGS reports EXACTLY (88/0x106, 89/0x107).  So resolve that precise
     * window+surface with no enumeration -> any number of GL windows disambiguate regardless of z-order.
     * (d[0] is the SAME across windows, so the old d[0] heuristic only worked by z-order luck.) */
    if (window_id) {
        CGRect fr = {{0,0},{0,0}}, sb = {{0,0},{0,0}};
        if (CGSGetScreenRectForWindow(cid, (CGSWindowID)window_id, &fr) == 0 &&
            CGSGetSurfaceBounds(cid, (CGSWindowID)window_id, (CGSSurfaceID)surface_id, &sb) == 0 &&
            sb.size.width > 0 && sb.size.height > 0) {
            *ox = (int)(fr.origin.x + sb.origin.x);
            *oy = (int)(fr.origin.y + sb.origin.y);
            *ow = (int)sb.size.width; *oh = (int)sb.size.height;
            gld_log("  [cgs] win %u surf 0x%x -> screen (%d,%d) %dx%d (direct)\n",
                    window_id, surface_id, *ox, *oy, *ow, *oh);
            return 1;
        }
    }

    /* FALLBACK: first window with a surface -- used only if the descriptor ids don't resolve (e.g. a
     * fullscreen/garbage descriptor, or a GLEngine build where d[1]/d[2] aren't CGS ids).  Correct for
     * the single-window case; ambiguous for multiple (which the direct path above handles). */
    if (CGSGetWindowList(cid, cid, 256, wins, &nwin) != 0 || nwin <= 0) return 0;
    for (i = 0; i < nwin; i++) {
        CGRect fr; CGSSurfaceID surfs[64]; int ns = 0;
        if (CGSGetScreenRectForWindow(cid, wins[i], &fr) != 0) continue;
        if (CGSGetSurfaceList(cid, wins[i], 64, surfs, &ns) != 0 || ns <= 0) continue;
        for (j = 0; j < ns; j++) {
            CGRect sb;
            if (CGSGetSurfaceBounds(cid, wins[i], surfs[j], &sb) != 0) continue;
            if (sb.size.width <= 0 || sb.size.height <= 0) continue;
            *ox = (int)(fr.origin.x + sb.origin.x);
            *oy = (int)(fr.origin.y + sb.origin.y);
            *ow = (int)sb.size.width; *oh = (int)sb.size.height;
            gld_log("  [cgs] win %d surf 0x%x -> screen (%d,%d) %dx%d (fallback, %d win)\n",
                    wins[i], (unsigned)surfs[j], *ox, *oy, *ow, *oh, nwin);
            return 1;
        }
    }
    gld_log("  [cgs] no resolvable GL surface (win=%u surf=0x%x, %d windows)\n",
            window_id, surface_id, nwin);
    return 0;
}

/* Main-display pixel size (public CoreGraphics).  A FULLSCREEN drawable descriptor carries no usable
 * geometry (it is reused heap -- observed holding ASCII "fsprobe"), so its d[1]/d[2] are garbage and
 * Apple's set_shape rejects them (0xe00002be) -> no render target.  Shape the fullscreen surface to
 * the real display size instead.  Returns 1 + w/h on success (e.g. 1024x768 on the Lombard panel). */
int mach64_display_size(int *w, int *h)
{
    CGDirectDisplayID d = CGMainDisplayID();
    size_t pw = CGDisplayPixelsWide(d), ph = CGDisplayPixelsHigh(d);
    if (pw > 0 && ph > 0 && pw <= 4096 && ph <= 4096) {
        *w = (int)pw; *h = (int)ph;
        gld_log("  [cgs] main display = %dx%d\n", *w, *h);
        return 1;
    }
    return 0;
}
