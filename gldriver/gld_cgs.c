/* gld_cgs.c - resolve the windowed drawable's on-screen content bounds via the private CoreGraphics
 * CGS API (userspace, NO kext), for the windowed render+blit path.  GLEngine hands our bundle only
 * a surface-ID (drawable word[0], e.g. 0x8013); the actual window geometry lives in CGS.
 *
 * Validated resolver (mach64_wintest, Lombard): for the app's GL window, combine the window's screen
 * FRAME rect with the surface's WINDOW-RELATIVE bounds:
 *     screen_content.origin = CGSGetScreenRectForWindow(win).origin + CGSGetSurfaceBounds(surf).origin
 *     screen_content.size   = surface bounds size
 * e.g. window frame (120,58 400x322) + surface (0,22 400x300) = content (120,80 400x300).
 *
 * The CGS surface id (0x101) is a DIFFERENT namespace from GLEngine's drawable-id (0x80xx), so we do
 * NOT match by id -- for the common single GL window it's unambiguous (take the first window that
 * has a surface).  Replaces the MACH64_WIN_RECT env injection; returns 0 (caller falls back to the
 * env var / fullscreen path) if nothing resolves.  Pure CoreGraphics -> safe, no panic path. */
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

int mach64_cgs_resolve_bounds(uint32_t surface_id, int *ox, int *oy, int *ow, int *oh)
{
    CGSConnectionID cid = _CGSDefaultConnection();
    if (!cid) return 0;
    CGSWindowID wins[256]; int nwin = 0, i, j;
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
            *ow = (int)sb.size.width;
            *oh = (int)sb.size.height;
            gld_log("  [cgs] sid=0x%x -> win %d surf 0x%x -> screen (%d,%d) %dx%d\n",
                    surface_id, wins[i], (unsigned)surfs[j], *ox, *oy, *ow, *oh);
            return 1;
        }
    }
    gld_log("  [cgs] no resolvable GL surface among %d app windows (sid=0x%x)\n", nwin, surface_id);
    return 0;
}
