/* cursor_walk: exercise the RageStubFB hardware cursor.  Run in the GUI session.  Warps the pointer to points on the Rage display (found by its 640-pixel-wide
 * or 'rage' id argument) and after each move asks the stub for a snapshot of the cursor registers (property rage-cursor-regs -> RageCursorRegs).
 *   cursor_walk stay         only park the pointer in the middle of the Rage display
 *   cursor_walk              walk a fixed set of points: centre, corners, the left/top edges (negative hot-spot clip) and back to the main display
 * Prints "x y -> registers" lines.  Exit status 1 if the hardware cursor was never enabled. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void regs(const char *tag)
{
    io_service_t s = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("RageStubFB"));
    if (!s) { printf("%s: no stub\n", tag); return; }
    CFStringRef k = CFSTR("rage-cursor-regs"); int one = 1; CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &one);
    CFDictionaryRef d = CFDictionaryCreate(NULL, (const void **)&k, (const void **)&v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IORegistryEntrySetCFProperties(s, d); CFRelease(d); CFRelease(v);
    CFTypeRef r = IORegistryEntryCreateCFProperty(s, CFSTR("RageCursorRegs"), NULL, 0);
    char b[256] = "?"; if (r && CFGetTypeID(r) == CFStringGetTypeID()) CFStringGetCString((CFStringRef)r, b, sizeof b, kCFStringEncodingASCII);
    printf("%-28s %s\n", tag, b); fflush(stdout);
    if (r) CFRelease(r);
    IOObjectRelease(s);
}
int main(int argc, char **argv)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i; CGDirectDisplayID rage = 0; CGRect rb;
    CGGetActiveDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) { CGRect b = CGDisplayBounds(ids[i]); if (CGDisplayPixelsWide(ids[i]) <= 1280 && CGDisplayIDToOpenGLDisplayMask(ids[i])) { rage = ids[i]; rb = b; } }
    if (!rage) { printf("no Rage display found (expect a second display not at the origin)\n"); return 2; }
    printf("Rage display %u at %.0f,%.0f %.0fx%.0f\n", (unsigned)rage, rb.origin.x, rb.origin.y, rb.size.width, rb.size.height);
    regs("before");
    struct { const char *name; float fx, fy; int dx, dy; } pts[] = {
        { "centre", .5, .5, 0, 0 }, { "left edge", 0, .5, 1, 0 }, { "top edge", .5, 0, 0, 1 }, { "top-left", 0, 0, 1, 1 },
        { "bottom-right", 1, 1, -2, -2 }, { "right edge", 1, .5, -2, 0 }, { "mid again", .3, .6, 0, 0 } };
    int k, enabled = 0;
    for (k = 0; k < (int)(sizeof pts / sizeof pts[0]); k++) {
        CGPoint p = CGPointMake(rb.origin.x + pts[k].fx * (rb.size.width - 1) + pts[k].dx, rb.origin.y + pts[k].fy * (rb.size.height - 1) + pts[k].dy);
        CGWarpMouseCursorPosition(p); CGPostMouseEvent(p, 1, 1, 0);   /* the warp alone does not always reach the HID cursor path; the event with no buttons does */
        usleep(500000);
        char tag[64]; snprintf(tag, sizeof tag, "%s (%.0f,%.0f)", pts[k].name, p.x, p.y); regs(tag);
    }
    if (argc > 1 && !strcmp(argv[1], "stay")) {             /* leave the pointer in the middle of the Rage display */
        CGPoint c = CGPointMake(rb.origin.x + rb.size.width / 2, rb.origin.y + rb.size.height / 2); CGWarpMouseCursorPosition(c); CGPostMouseEvent(c, 1, 1, 0); usleep(300000); regs("parked on the Rage"); return 0; }
    CGPoint home = CGPointMake(100, 100); CGWarpMouseCursorPosition(home); CGPostMouseEvent(home, 1, 1, 0); usleep(500000); regs("back on main display");
    (void)enabled; return 0;
}
