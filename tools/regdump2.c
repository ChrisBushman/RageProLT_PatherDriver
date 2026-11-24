/* regdump2.c: read-only dump of mach64 drawing/3D registers 0x280..0x7fc through the aperture register window (BAR0+0x7FF800). usage: regdump2 > FILE */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdint.h>
#include "../common/mach64_abi.h"
int main(void)
{
    io_service_t ms = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    io_connect_t mc = 0; vm_address_t fa = 0; vm_size_t fz = 0; unsigned r;
    if (!ms || IOServiceOpen(ms, mach_task_self(), MACH64_GL_CONNECT_TYPE, &mc) ||
        IOConnectMapMemory(mc, MACH64_MEM_SURFACE, mach_task_self(), &fa, &fz, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "cannot map\n"); return 1; }
    for (r = 0x280; r < 0x800; r += 4)
        printf("reg 0x%03x = 0x%08x\n", r, (unsigned)OSReadLittleInt32((void *)(fa + 0x7FF800), r));
    return 0;
}
