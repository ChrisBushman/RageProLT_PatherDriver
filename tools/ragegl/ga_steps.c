/* tools/ragegl/ga_steps.c - replays the first steps of ATIRageProGA Start: IOAccelFindAccelerator, IOObjectConformsTo(accel,"ATIRagePro"), IOServiceOpen type 2. */
#include <stdio.h>
#include <IOKit/IOKitLib.h>
extern kern_return_t IOAccelFindAccelerator(io_service_t fb, io_service_t *accel, UInt32 *index);
int main(void)
{
    mach_port_t m; io_service_t fb, acc = 0; UInt32 idx = 99; kern_return_t kr; io_connect_t c;
    IOMasterPort(MACH_PORT_NULL, &m);
    fb = IOServiceGetMatchingService(m, IOServiceMatching("RageStubFB"));
    kr = IOAccelFindAccelerator(fb, &acc, &idx);
    printf("IOAccelFindAccelerator 0x%x accel %u index %u\n", kr, (unsigned)acc, (unsigned)idx);
    if (kr) return 1;
    printf("conforms ATIRagePro: %d  IOAccelerator: %d\n", (int)IOObjectConformsTo(acc, "ATIRagePro"), (int)IOObjectConformsTo(acc, "IOAccelerator"));
    kr = IOServiceOpen(acc, mach_task_self(), 2, &c);
    printf("IOServiceOpen type 2: 0x%x\n", kr);
    return 0;
}
