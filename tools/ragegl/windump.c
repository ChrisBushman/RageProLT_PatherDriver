/* windump: list the on-screen windows front to back with the CGS properties that tell whether the window server composites them with transparency
 * (frame, alpha, level, type, tags, opaque shape).  Run in the GUI session (rungui.sh).  Panther private CGS API. */
#include <stdio.h>
#include <stdlib.h>
#include <ApplicationServices/ApplicationServices.h>
typedef int CGSConnectionID; typedef int CGSWindowID;
extern CGSConnectionID _CGSDefaultConnection(void);
extern int CGSGetOnScreenWindowList(CGSConnectionID, CGSConnectionID, int, CGSWindowID *, int *);
extern int CGSGetScreenRectForWindow(CGSConnectionID, CGSWindowID, CGRect *);
extern int CGSGetWindowAlpha(CGSConnectionID, CGSWindowID, float *);
extern int CGSGetWindowLevel(CGSConnectionID, CGSWindowID, int *);
extern int CGSGetWindowType(CGSConnectionID, CGSWindowID, int *);
extern int CGSGetWindowTags(CGSConnectionID, CGSWindowID, int *tags, int len);
extern int CGSGetWindowOpaqueShape(CGSConnectionID, CGSWindowID, void **shape);
extern int CGSGetWindowShape(CGSConnectionID, CGSWindowID, void **shape);
extern int CGSRegionIsEmpty(void *region);
extern int CGSGetRegionBounds(void *region, CGRect *r);
extern int CGSWindowIsAccelerated(CGSConnectionID, CGSWindowID);
int main(int argc, char **argv)
{
    int mask = argc > 1 ? atoi(argv[1]) : 0;        /* 1 = tags, 2 = opaque shape, 4 = IsAccelerated (signatures guessed: a wrong one crashes) */
    CGSConnectionID c = _CGSDefaultConnection(); CGSWindowID w[256]; int n = 0, i;
    if (CGSGetOnScreenWindowList(c, 0, 256, w, &n)) { printf("list failed\n"); return 1; }
    for (i = 0; i < n; i++) {
        CGRect r = {{0,0},{0,0}}; float a = -1; int lvl = -99, ty = -99, tg[2] = {0,0}; void *sh = (void *)-1; int rc;
        CGSGetScreenRectForWindow(c, w[i], &r); CGSGetWindowAlpha(c, w[i], &a); CGSGetWindowLevel(c, w[i], &lvl); CGSGetWindowType(c, w[i], &ty);
        if (mask & 1) CGSGetWindowTags(c, w[i], tg, 2);
        rc = (mask & 2) ? CGSGetWindowOpaqueShape(c, w[i], &sh) : -1;
        if (mask & 8) {   /* shape and opaque shape: empty? bounds? */
            void *shp = 0, *ops = 0; CGRect sb = {{0,0},{0,0}}, ob = {{0,0},{0,0}}; int src, orc, se = -1, oe = -1;
            src = CGSGetWindowShape(c, w[i], &shp); orc = CGSGetWindowOpaqueShape(c, w[i], &ops);
            if (!src && shp) { se = CGSRegionIsEmpty(shp); CGSGetRegionBounds(shp, &sb); }
            if (!orc && ops) { oe = CGSRegionIsEmpty(ops); CGSGetRegionBounds(ops, &ob); }
            printf("%2d id %4d frame %4d,%4d %4dx%-4d | shape rc=%d empty=%d bounds %d,%d %dx%d | opaque rc=%d empty=%d bounds %d,%d %dx%d\n", i, w[i], (int)r.origin.x, (int)r.origin.y,
                   (int)r.size.width, (int)r.size.height, src, se, (int)sb.origin.x, (int)sb.origin.y, (int)sb.size.width, (int)sb.size.height,
                   orc, oe, (int)ob.origin.x, (int)ob.origin.y, (int)ob.size.width, (int)ob.size.height);
            continue;
        }
        printf("%2d id %4d  %4d,%4d %4dx%-4d alpha %.2f level %d type %d tags %08x %08x opaqueshape rc=%d %p accel=%d\n", i, w[i], (int)r.origin.x, (int)r.origin.y,
               (int)r.size.width, (int)r.size.height, a, lvl, ty, (unsigned)tg[0], (unsigned)tg[1], rc, sh, (mask & 4) ? CGSWindowIsAccelerated(c, w[i]) : -1);
    }
    return 0;
}
