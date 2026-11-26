/* chipinfo: ask RageStubFB for a read-only snapshot of the chip configuration registers (property rage-chipinfo -> RageChipInfo) and print it. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
int main(void)
{
    io_service_t s = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("RageStubFB"));
    if (!s) { printf("no stub\n"); return 1; }
    CFStringRef k = CFSTR("rage-chipinfo"); int one = 1; CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &one);
    CFDictionaryRef d = CFDictionaryCreate(NULL, (const void **)&k, (const void **)&v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IORegistryEntrySetCFProperties(s, d);
    CFTypeRef r = IORegistryEntryCreateCFProperty(s, CFSTR("RageChipInfo"), NULL, 0);
    char b[300] = "?"; if (r && CFGetTypeID(r) == CFStringGetTypeID()) CFStringGetCString((CFStringRef)r, b, sizeof b, kCFStringEncodingASCII);
    printf("%s\n", b); return 0;
}
