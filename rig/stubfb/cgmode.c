/* cgmode                 : list every active display (id, size, bounds, vendor/model)
 * cgmode ID              : list the modes of display ID
 * cgmode ID W H [secs [force]] : switch display ID to WxH at 16 bpp (what System Preferences does).  Refuses the main display. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    CGDirectDisplayID ids[16]; CGDisplayCount n = 0, i;
    CGGetActiveDisplayList(16, ids, &n);
    if (argc == 1) {
        for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]);
            printf("display id %u: %dx%d at (%d,%d) main=%d vendor %x model %x\n", (unsigned)ids[i], (int)b.size.width, (int)b.size.height, (int)b.origin.x, (int)b.origin.y,
                   (int)CGDisplayIsMain(ids[i]), (unsigned)CGDisplayVendorNumber(ids[i]), (unsigned)CGDisplayModelNumber(ids[i])); }
        return 0;
    }
    CGDirectDisplayID d = (CGDirectDisplayID)atoi(argv[1]); int found = 0;
    for (i = 0; i < n; i++) if (ids[i] == d) found = 1;
    if (!found) { printf("no such display\n"); return 1; }
    if (argc == 2) {
        CFArrayRef a = CGDisplayAvailableModes(d); CFIndex k, c = a ? CFArrayGetCount(a) : 0;
        for (k = 0; k < c; k++) { CFDictionaryRef m = CFArrayGetValueAtIndex(a, k); int w, h, b, hz = 0; CFNumberRef r = CFDictionaryGetValue(m, kCGDisplayRefreshRate);
            CFNumberGetValue(CFDictionaryGetValue(m, kCGDisplayWidth), kCFNumberIntType, &w); CFNumberGetValue(CFDictionaryGetValue(m, kCGDisplayHeight), kCFNumberIntType, &h);
            CFNumberGetValue(CFDictionaryGetValue(m, kCGDisplayBitsPerPixel), kCFNumberIntType, &b); if (r) CFNumberGetValue(r, kCFNumberIntType, &hz);
            printf("mode %d: %dx%d %dbpp %dHz\n", (int)k, w, h, b, hz); }
        return 0;
    }
    if (CGDisplayIsMain(d) && argc < 6) { printf("refusing: that is the main display (add a 5th argument, e.g. force, if the Rage was made primary on purpose)\n"); return 1; }
    boolean_t exact; CFDictionaryRef m = CGDisplayBestModeForParameters(d, 16, atoi(argv[2]), atoi(argv[3]), &exact);
    printf("best mode exact=%d\n", (int)exact); if (!exact) return 1;
    printf("switch -> %d\n", (int)CGDisplaySwitchToMode(d, m)); fflush(stdout);
    sleep(argc > 4 ? atoi(argv[4]) : 25);   /* the switch is undone when this process exits: stay alive while it is looked at */
    printf("exiting: WindowServer restores the previous mode\n");
    return 0;
}
