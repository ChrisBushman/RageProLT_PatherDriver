/* restore_desktop.c - force the WindowServer to reconfigure + redraw the main display,
 * without a reboot.  Our GL demo left the mach64 CRTC scanning out our render buffer and
 * overwrote the desktop pixels in VRAM; a capture/release cycle makes CoreGraphics
 * reprogram the CRTC and hand the display back to the WindowServer, which recomposites the
 * whole desktop (the same path a full-screen app takes when it exits).
 * Build on the Lombard:  cc -o restore_desktop restore_desktop.c -framework ApplicationServices
 * Run:                   ./restore_desktop        (try plain first, then with sudo) */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    CGDirectDisplayID d = CGMainDisplayID();
    CGError e;

    printf("main display = %u\n", (unsigned)d);

    e = CGDisplayCapture(d);
    printf("CGDisplayCapture -> %d %s\n", (int)e, e == kCGErrorSuccess ? "OK" : "FAIL");
    if (e != kCGErrorSuccess) {
        /* Capture failed (likely no WindowServer session from ssh).  Try a mode re-set as a
         * fallback: switching to the current mode still forces a reconfigure on some drivers. */
        CFDictionaryRef m = CGDisplayCurrentMode(d);
        e = CGDisplaySwitchToMode(d, m);
        printf("CGDisplaySwitchToMode -> %d\n", (int)e);
        return (e == kCGErrorSuccess) ? 0 : 2;
    }

    /* Paint the captured display black so there's no flash of stale content, then release
     * it back to the WindowServer, which redraws the desktop. */
    {   CGContextRef ctx = CGDisplayGetDrawingContext(d);
        if (ctx) {
            CGRect r = CGRectMake(0, 0, CGDisplayPixelsWide(d), CGDisplayPixelsHigh(d));
            CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
            CGContextFillRect(ctx, r);
        }
    }
    sleep(1);
    e = CGDisplayRelease(d);
    printf("CGDisplayRelease -> %d %s\n", (int)e, e == kCGErrorSuccess ? "OK" : "FAIL");
    return 0;
}
