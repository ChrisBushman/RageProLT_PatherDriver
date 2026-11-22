/* mach64_modeswitch.c -- switch the main display's resolution from the command line (display-mode-change testing).
 *
 *   cc -o mach64_modeswitch mach64_modeswitch.c -framework ApplicationServices
 *   ./mach64_modeswitch            # print the current mode
 *   ./mach64_modeswitch 640 480    # switch (keeps the current depth / best refresh)
 *
 * Runs as the console user (it needs the WindowServer).  The switch is permanent for the session, like System Preferences'. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>

static long dict_long(CFDictionaryRef d, CFStringRef k)
{
    long v = 0; CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(d, k);
    if (n) CFNumberGetValue(n, kCFNumberLongType, &v);
    return v;
}

int main(int argc, char **argv)
{
    CGDirectDisplayID disp = CGMainDisplayID();
    CFDictionaryRef cur = CGDisplayCurrentMode(disp), want;
    boolean_t exact = 0;
    CGDisplayErr err;
    printf("current: %ldx%ld %ld bpp\n", dict_long(cur, kCGDisplayWidth), dict_long(cur, kCGDisplayHeight), dict_long(cur, kCGDisplayBitsPerPixel));
    if (argc < 3) return 0;
    want = CGDisplayBestModeForParameters(disp, (size_t)dict_long(cur, kCGDisplayBitsPerPixel), (size_t)atoi(argv[1]), (size_t)atoi(argv[2]), &exact);
    if (!want) { fprintf(stderr, "no mode for %sx%s\n", argv[1], argv[2]); return 1; }
    printf("switching to %ldx%ld %ld bpp%s\n", dict_long(want, kCGDisplayWidth), dict_long(want, kCGDisplayHeight), dict_long(want, kCGDisplayBitsPerPixel), exact ? "" : " (closest)");
    fflush(stdout);
    {   CGDisplayConfigRef cfg;
        if ((err = CGBeginDisplayConfiguration(&cfg)) != kCGErrorSuccess) { fprintf(stderr, "begin: %d\n", (int)err); return 1; }
        CGConfigureDisplayMode(cfg, disp, want);
        if ((err = CGCompleteDisplayConfiguration(cfg, kCGConfigureForSession)) != kCGErrorSuccess) { fprintf(stderr, "complete: %d\n", (int)err); return 1; }
    }
    cur = CGDisplayCurrentMode(disp);
    printf("now: %ldx%ld %ld bpp\n", dict_long(cur, kCGDisplayWidth), dict_long(cur, kCGDisplayHeight), dict_long(cur, kCGDisplayBitsPerPixel));
    return 0;
}
