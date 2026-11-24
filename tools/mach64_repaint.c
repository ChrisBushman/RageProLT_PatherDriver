/* mach64_repaint.c - force WindowServer to repaint the whole 640x480 (Rage) display.
 * Our headless tests (suite present test, fbpattern, mach64_present) write the scanout directly; WindowServer does not know, so those
 * pixels stay until something invalidates them.  This opens a plain window over the display and closes it again.  Run in the GUI session.
 * Build: cc -o mach64_repaint mach64_repaint.c -framework Carbon -framework ApplicationServices */
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <unistd.h>
int main(void)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, k; WindowRef win = NULL; Rect r = {0, 0, 0, 0}; int found = 0;
    CGGetActiveDisplayList(8, ids, &n);
    for (k = 0; k < n; k++) {
        CGRect b = CGDisplayBounds(ids[k]);
        if ((int)b.size.width == 640 && (int)b.size.height == 480) {
            r.left = (short)b.origin.x; r.top = (short)b.origin.y; r.right = r.left + 640; r.bottom = r.top + 480; found = 1;
        }
    }
    if (!found) { fprintf(stderr, "no 640x480 display\n"); return 1; }
    if (CreateNewWindow(kPlainWindowClass, kWindowNoShadowAttribute, &r, &win) != noErr || !win) { fprintf(stderr, "CreateNewWindow failed\n"); return 2; }
    ShowWindow(win);
    { int i; for (i = 0; i < 10; i++) { EventRef e; if (ReceiveNextEvent(0, NULL, 0.1, true, &e) == noErr) { SendEventToEventTarget(e, GetEventDispatcherTarget()); ReleaseEvent(e); } } }
    DisposeWindow(win);
    { int i; for (i = 0; i < 10; i++) { EventRef e; if (ReceiveNextEvent(0, NULL, 0.1, true, &e) == noErr) { SendEventToEventTarget(e, GetEventDispatcherTarget()); ReleaseEvent(e); } } }
    printf("repainted (%d,%d %dx%d)\n", r.left, r.top, 640, 480);
    return 0;
}
