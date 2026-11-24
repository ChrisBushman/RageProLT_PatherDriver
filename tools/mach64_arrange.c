/* mach64_arrange.c - move the 640x480 display (the Rage) to the origin so it becomes the primary display (menu bar, Dock), or put the layout back.
 *   mach64_arrange show                 list displays with origins
 *   mach64_arrange rage [perm]          Rage at (0,0); every other display placed to its right, in their current left-to-right order
 *   mach64_arrange restore [perm]       the 4K main display back at (0,0), Rage to its right
 *   mach64_arrange set KIND X Y         move ONE display (KIND = rage | main | phantom, the 0x0 one) to (X,Y), others untouched
 *   mach64_arrange mirror KIND          make that display mirror the main display
 *   mach64_arrange disable KIND | enable KIND   switch a display off / on (private CGSConfigureDisplayEnabled); KIND = rage | main | phantom
 * Default scope is this login session only (a reboot restores the saved arrangement); "perm" stores it (kCGConfigurePermanently).
 * Run in the GUI session.  Build: cc -o mach64_arrange mach64_arrange.c -framework ApplicationServices */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern CGError CGSConfigureDisplayEnabled(CGDisplayConfigRef config, CGDirectDisplayID display, int enabled);
static int cmpx(const void *a, const void *b) { CGDirectDisplayID x = *(const CGDirectDisplayID *)a, y = *(const CGDirectDisplayID *)b; return (int)CGDisplayBounds(x).origin.x - (int)CGDisplayBounds(y).origin.x; }
int main(int argc, char **argv)
{
    CGDirectDisplayID ids[16]; CGDisplayCount n = 0, i; const char *cmd = argc > 1 ? argv[1] : "show"; int perm = argc > 2 && !strcmp(argv[argc - 1], "perm");
    CGGetOnlineDisplayList(16, ids, &n); qsort(ids, n, sizeof ids[0], cmpx);
    for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]); printf("id 0x%x (%d,%d) %dx%d%s\n", (unsigned)ids[i], (int)b.origin.x, (int)b.origin.y, (int)b.size.width, (int)b.size.height, CGDisplayIsMain(ids[i]) ? " MAIN" : ""); }
    if (!strcmp(cmd, "show")) return 0;
    if (!strcmp(cmd, "set") || !strcmp(cmd, "mirror") || !strcmp(cmd, "disable") || !strcmp(cmd, "enable")) {
        CGDisplayConfigRef cfg; CGDirectDisplayID t = 0, mainid = 0; CGError e; const char *kind = argc > 2 ? argv[2] : "";
        for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]); int w = (int)b.size.width;
            if (CGDisplayIsMain(ids[i])) mainid = ids[i];
            if ((!strcmp(kind, "rage") && w == 640) || (!strcmp(kind, "main") && w >= 1024) || (!strcmp(kind, "phantom") && w == 0)) t = ids[i]; }
        if (!t) { fprintf(stderr, "no display of kind %s\n", kind); return 1; }
        if ((e = CGBeginDisplayConfiguration(&cfg)) != kCGErrorSuccess) { fprintf(stderr, "begin failed %d\n", (int)e); return 1; }
        if (!strcmp(cmd, "set")) { int x = argc > 3 ? atoi(argv[3]) : 0, y = argc > 4 ? atoi(argv[4]) : 0; CGConfigureDisplayOrigin(cfg, t, x, y); printf("-> %s 0x%x to (%d,%d)\n", kind, (unsigned)t, x, y); }
        else if (!strcmp(cmd, "disable") || !strcmp(cmd, "enable")) { int on = !strcmp(cmd, "enable"); CGError de = CGSConfigureDisplayEnabled(cfg, t, on); printf("-> %s 0x%x %s (CGSConfigureDisplayEnabled -> %d)\n", kind, (unsigned)t, on ? "enabled" : "disabled", (int)de); }
        else { CGConfigureDisplayMirrorOfDisplay(cfg, t, mainid); printf("-> %s 0x%x mirrors main 0x%x\n", kind, (unsigned)t, (unsigned)mainid); }
        e = CGCompleteDisplayConfiguration(cfg, perm ? kCGConfigurePermanently : kCGConfigureForSession); printf("complete (session) -> %d\n", (int)e); return 0;
    }
    {   CGDisplayConfigRef cfg; CGDirectDisplayID order[16]; int cnt = 0, x = 0, big = -1; CGError e;
        for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]); if ((int)b.size.width == 640 && (int)b.size.height == 480) { if (!strcmp(cmd, "rage")) order[cnt++] = ids[i]; } }
        if (!strcmp(cmd, "rage")) { for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]); if (!((int)b.size.width == 640 && (int)b.size.height == 480)) order[cnt++] = ids[i]; } }
        else if (!strcmp(cmd, "restore")) {
            for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]); if ((int)b.size.width >= 1024) { big = (int)i; order[cnt++] = ids[i]; break; } }
            for (i = 0; i < n; i++) if ((int)i != big) order[cnt++] = ids[i];
        } else { fprintf(stderr, "usage: show | rage [perm] | restore [perm]\n"); return 2; }
        if ((e = CGBeginDisplayConfiguration(&cfg)) != kCGErrorSuccess) { fprintf(stderr, "begin failed %d\n", (int)e); return 1; }
        for (i = 0; i < (CGDisplayCount)cnt; i++) { CGRect b = CGDisplayBounds(order[i]); CGConfigureDisplayOrigin(cfg, order[i], x, 0); printf("-> id 0x%x to (%d,0)\n", (unsigned)order[i], x); x += (int)b.size.width; }
        e = CGCompleteDisplayConfiguration(cfg, perm ? kCGConfigurePermanently : kCGConfigureForSession);
        printf("complete (%s) -> %d\n", perm ? "permanent" : "session", (int)e);
    }
    return 0;
}
