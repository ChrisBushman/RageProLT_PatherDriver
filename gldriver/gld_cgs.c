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
 * z-order luck.)  Returns 0 (caller falls back to the fullscreen path) if nothing resolves.  Pure CoreGraphics -> safe, no panic path. */
#include "gld.h"
#include <stdint.h>
#include <ApplicationServices/ApplicationServices.h>   /* CGRect */
#include <IOKit/IOKitLib.h>
#include <string.h>
#include <stdio.h>

/* Private CGS API (Panther), signatures reversed from CoreGraphics. */
typedef int CGSConnectionID; typedef int CGSWindowID; typedef int CGSSurfaceID;
extern CGSConnectionID _CGSDefaultConnection(void);
extern int CGSGetWindowList(CGSConnectionID cid, CGSConnectionID target, int cap, CGSWindowID *list, int *cnt);
extern int CGSGetSurfaceList(CGSConnectionID cid, CGSWindowID wid, int cap, CGSSurfaceID *list, int *cnt);
extern int CGSGetScreenRectForWindow(CGSConnectionID cid, CGSWindowID wid, CGRect *r);
extern int CGSGetOnScreenWindowList(CGSConnectionID cid, CGSConnectionID target, int cap, CGSWindowID *list, int *cnt);
extern int CGSGetSurfaceBounds(CGSConnectionID cid, CGSWindowID wid, CGSSurfaceID sid, CGRect *b);


/* The display THIS driver scans out.  CGS and CGDisplayBounds use one global desktop (the Rage can be any display, e.g. at x=3840 beside a
 * 4K main display); the blit destination and the fullscreen size are relative to / those of the Rage's own display, which is the one whose
 * IOFramebuffer carries an "IOAccelTypes" naming ATIRagePro.  Falls back to the main display (the single-display Lombard). */
static int rage_display(CGRect *b)
{
    CGDirectDisplayID ids[16]; CGDisplayCount n = 0, i;
    if (CGGetActiveDisplayList(16, ids, &n) == kCGErrorSuccess) {
        for (i = 0; i < n; i++) {
            io_service_t s = CGDisplayIOServicePort(ids[i]);
            CFTypeRef t = s ? IORegistryEntryCreateCFProperty(s, CFSTR("IOAccelTypes"), kCFAllocatorDefault, 0) : NULL;
            int hit = 0;
            if (t) {
                if (CFGetTypeID(t) == CFStringGetTypeID()) hit = CFStringFind((CFStringRef)t, CFSTR("ATIRagePro"), 0).location != kCFNotFound;
                CFRelease(t);
            }
            if (hit) { *b = CGDisplayBounds(ids[i]); return 1; }
        }
    }
    *b = CGDisplayBounds(CGMainDisplayID());
    return 0;
}

/* window rect in global desktop coordinates -> coordinates relative to the Rage display; 0 if it does not lie on that display */
static int g_off_display = 0;
int mach64_cgs_off_display(void) { return g_off_display; }

static int to_display_local(int *ox, int *oy, int w, int h)
{
    CGRect d; int found = rage_display(&d), x = *ox - (int)d.origin.x, y = *oy - (int)d.origin.y;
    if (!found && d.origin.x == 0 && d.origin.y == 0) return 1;   /* main display at the origin: unchanged (Lombard) */
    if (x + w <= 0 || y + h <= 0 || x >= (int)d.size.width || y >= (int)d.size.height) {
        g_off_display = 1;
        return 0;
    }
    *ox = x; *oy = y;
    return 1;
}

int mach64_cgs_resolve_bounds(uint32_t window_id, uint32_t surface_id, int allow_enum,
                              int *ox, int *oy, int *ow, int *oh)
{
    g_off_display = 0;                          /* set again by to_display_local if THIS window is on another display */
    CGSConnectionID cid = _CGSDefaultConnection();
    CGSWindowID wins[256]; int nwin = 0, i, j;
    g_off_display = 0;
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
            return to_display_local(ox, oy, *ow, *oh);
        }
    }

    /* FALLBACK: first window with a surface -- used only if the descriptor ids don't resolve (e.g. a
     * fullscreen/garbage descriptor, or a GLEngine build where d[1]/d[2] aren't CGS ids).  Correct for
     * the single-window case; ambiguous for multiple (which the direct path above handles).  Gated by
     * allow_enum: a fullscreen drawable's garbage ids must NOT falsely enumerate to some other app's
     * window -- only the exact direct match above is trusted for the ungated (single-buffered) case. */
    if (!allow_enum) {
        return 0;
    }
    if (CGSGetWindowList(cid, cid, 256, wins, &nwin) != 0 || nwin <= 0) {
        return 0;
    }
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
            return to_display_local(ox, oy, *ow, *oh);
        }
    }
    return 0;
}

/* Bytes per pixel of the display this driver scans out (CGDisplayBitsPerPixel: 16 for 15/16 bpp modes, 32 for 24/32). */
int mach64_display_bytes_per_pixel(void)
{
    CGDirectDisplayID ids[16]; CGDisplayCount n = 0, i; CGRect d; int found = rage_display(&d);
    if (!found) return 0;
    if (CGGetActiveDisplayList(16, ids, &n) != kCGErrorSuccess) return 0;
    for (i = 0; i < n; i++) {
        CGRect b = CGDisplayBounds(ids[i]);
        if ((int)b.origin.x == (int)d.origin.x && (int)b.origin.y == (int)d.origin.y && (int)b.size.width == (int)d.size.width)
            return (int)(CGDisplayBitsPerPixel(ids[i]) + 7) / 8;
    }
    return 0;
}

/* Main-display pixel size (public CoreGraphics).  A FULLSCREEN drawable descriptor carries no usable
 * geometry (it is reused heap -- observed holding ASCII "fsprobe"), so its d[1]/d[2] are garbage and
 * Apple's set_shape rejects them (0xe00002be) -> no render target.  Shape the fullscreen surface to
 * the real display size instead.  Returns 1 + w/h on success (e.g. 1024x768 on the Lombard panel). */
int mach64_display_size(int *w, int *h)
{
    CGRect b; size_t pw, ph;
    rage_display(&b);   /* the Rage's own display, not the main one (3840x2160 beside it on the MDD) */
    pw = (size_t)b.size.width; ph = (size_t)b.size.height;
    if (pw > 0 && ph > 0 && pw <= 4096 && ph <= 4096) {
        *w = (int)pw; *h = (int)ph;
        return 1;
    }
    return 0;
}

/* Visible part of a GL window: its rect minus the rects of every on-screen window in FRONT of it (CGSGetOnScreenWindowList is ordered front
 * to back).  Rects are display-local like the window rect passed in (lx,ly,w,h).  Returns the number of rects (>=0), or -1 if the window is
 * not in the list / CGS fails (caller keeps the full rect).  Capped at maxr; on overflow returns the full rect (never a wrong clip). */
int mach64_cgs_visible_rects(uint32_t window_id, uint32_t surface_id, int lx, int ly, int w, int h, short (*out)[4], int maxr)
{
    CGSConnectionID cid = _CGSDefaultConnection();
    CGSWindowID wins[256]; int nwin = 0, i, n = 1, pos = -1;
    CGRect fr = {{0,0},{0,0}}, sb = {{0,0},{0,0}};
    int gx, gy, dx, dy;
    int rc[64][4];
    if (!cid || maxr < 1 || maxr > 64) return -1;
    if (CGSGetOnScreenWindowList(cid, 0, 256, wins, &nwin) != 0 || nwin <= 0) return -1;
    for (i = 0; i < nwin; i++) if ((uint32_t)wins[i] == window_id) { pos = i; break; }
    if (pos < 0) return -1;
    if (CGSGetScreenRectForWindow(cid, (CGSWindowID)window_id, &fr) != 0 ||
        CGSGetSurfaceBounds(cid, (CGSWindowID)window_id, (CGSSurfaceID)surface_id, &sb) != 0) return -1;
    gx = (int)(fr.origin.x + sb.origin.x); gy = (int)(fr.origin.y + sb.origin.y);
    dx = lx - gx; dy = ly - gy;                 /* global -> display-local offset */
    rc[0][0] = lx; rc[0][1] = ly; rc[0][2] = lx + w; rc[0][3] = ly + h;   /* x0,y0,x1,y1 */
    for (i = 0; i < pos && n > 0; i++) {
        CGRect o; int ox0, oy0, ox1, oy1, k, m = 0; int nr[64][4];
        if (CGSGetScreenRectForWindow(cid, wins[i], &o) != 0) continue;
        if (o.size.width <= 0 || o.size.height <= 0) continue;
        ox0 = (int)o.origin.x + dx; oy0 = (int)o.origin.y + dy;
        ox1 = ox0 + (int)o.size.width; oy1 = oy0 + (int)o.size.height;
        for (k = 0; k < n; k++) {
            int *r = rc[k];
            int ix0 = r[0] > ox0 ? r[0] : ox0, iy0 = r[1] > oy0 ? r[1] : oy0;
            int ix1 = r[2] < ox1 ? r[2] : ox1, iy1 = r[3] < oy1 ? r[3] : oy1;
            if (ix0 >= ix1 || iy0 >= iy1) { if (m < 64) { nr[m][0]=r[0]; nr[m][1]=r[1]; nr[m][2]=r[2]; nr[m][3]=r[3]; m++; } continue; }
            /* up to four remainders: top, bottom, left, right of the intersection */
            if (r[1] < iy0 && m < 64) { nr[m][0]=r[0]; nr[m][1]=r[1]; nr[m][2]=r[2]; nr[m][3]=iy0; m++; }
            if (iy1 < r[3] && m < 64) { nr[m][0]=r[0]; nr[m][1]=iy1; nr[m][2]=r[2]; nr[m][3]=r[3]; m++; }
            if (r[0] < ix0 && m < 64) { nr[m][0]=r[0]; nr[m][1]=iy0; nr[m][2]=ix0; nr[m][3]=iy1; m++; }
            if (ix1 < r[2] && m < 64) { nr[m][0]=ix1; nr[m][1]=iy0; nr[m][2]=r[2]; nr[m][3]=iy1; m++; }
        }
        if (m >= 60) return -1;                 /* too fragmented: keep the full rect rather than clip wrongly */
        for (k = 0; k < m; k++) { rc[k][0]=nr[k][0]; rc[k][1]=nr[k][1]; rc[k][2]=nr[k][2]; rc[k][3]=nr[k][3]; }
        n = m;
    }
    if (n > maxr) return -1;
    for (i = 0; i < n; i++) { out[i][0]=(short)rc[i][0]; out[i][1]=(short)rc[i][1]; out[i][2]=(short)(rc[i][2]-rc[i][0]); out[i][3]=(short)(rc[i][3]-rc[i][1]); }
    return n;
}
