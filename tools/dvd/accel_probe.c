/* accel_probe: for each active display print CGDisplayUsesOpenGLAcceleration, its IOFramebuffer service and the IOAccelFindAccelerator result
 * (the two checks DVDPlayback imports).  Build: cc -o accel_probe accel_probe.c -framework ApplicationServices -framework IOKit -framework CoreFoundation */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <stdio.h>
extern kern_return_t IOAccelFindAccelerator(io_service_t framebuffer, io_service_t *accelerator, UInt32 *index);
int main(void)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i;
    CGGetActiveDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) {
        io_service_t fb = CGDisplayIOServicePort(ids[i]); io_service_t acc = 0; UInt32 idx = 0; kern_return_t kr = -1; io_name_t name = "?", cls = "?";
        if (fb) { IORegistryEntryGetName(fb, name); IOObjectGetClass(fb, cls); kr = IOAccelFindAccelerator(fb, &acc, &idx); }
        io_name_t an = "-"; if (kr == 0 && acc) IOObjectGetClass(acc, an);
        printf("display %lu %lux%lu: GL accel %d | fb service %u (%s, class %s) | IOAccelFindAccelerator -> %d accel class %s index %u\n", (unsigned long)ids[i],
               (unsigned long)CGDisplayPixelsWide(ids[i]), (unsigned long)CGDisplayPixelsHigh(ids[i]), (int)CGDisplayUsesOpenGLAcceleration(ids[i]), (unsigned)fb, name, cls, (int)kr, an, (unsigned)idx);
    }
    return 0;
}
