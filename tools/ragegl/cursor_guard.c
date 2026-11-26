/* tools/ragegl/cursor_guard.c - verify that nothing wrote into the block below the frame buffer (Apple's NDRV keeps the hardware-cursor image there).
 * usage (run as root): cursor_guard fill   - refill the canary pattern (RageStubFB, NDRV layout)
 *                      cursor_guard        - count the bytes of the block that changed; exit 0 when none did, 2 otherwise
 * docs/re-notes/apple-ndrv-framebuffer.md section 6/7. */
#include <stdio.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
int main(int argc, char **argv)
{
    int fill = argc > 1 && !strcmp(argv[1], "fill"); io_service_t s = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("RageStubFB")); int one = 1;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &one); CFStringRef k = CFStringCreateWithCString(NULL, fill ? "rage-cursor-fill" : "rage-cursor-check", kCFStringEncodingASCII);
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks); kern_return_t kr; int bad = -1, first = -1;
    if (!s) { printf("no RageStubFB\n"); return 99; }
    CFDictionarySetValue(d, k, v); kr = IORegistryEntrySetCFProperties(s, d);
    if (kr) { printf("set properties failed 0x%x (run as root)\n", kr); return 98; }
    if (fill) { printf("canary refilled\n"); return 0; }
    { CFTypeRef r = IORegistryEntryCreateCFProperty(s, CFSTR("RageCursorCheck"), NULL, 0); if (r) { CFNumberGetValue((CFNumberRef)r, kCFNumberIntType, &bad); CFRelease(r); }
      r = IORegistryEntryCreateCFProperty(s, CFSTR("RageCursorCheckFirst"), NULL, 0); if (r) { CFNumberGetValue((CFNumberRef)r, kCFNumberIntType, &first); CFRelease(r); } }
    if (bad < 0) { printf("no result (stub without the NDRV layout?)\n"); return 97; }
    if (bad) printf("  FAIL  %d bytes of the block below the frame buffer changed (first at 0x%x)\n", bad, first); else printf("  PASS  the block below the frame buffer is intact\n");
    printf("CURSOR_GUARD: %s\n", bad ? "FAILED" : "passed");
    return bad ? 2 : 0;
}
