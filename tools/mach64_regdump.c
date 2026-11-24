/* mach64_regdump.c - dump the chip's I/O register file (dwords 0x00..0xfc via the type-7 window) and the 256-entry DAC palette, to compare two
 * initialisations (ROM under emulation vs the stub's replay).  Skips registers whose read has side effects or that change by themselves:
 * CRTC_VLINE (0x10), CRTC_INT_CNTL status bits (0x18), and the DAC data port (read through the palette loop below).
 *   sudo ./mach64_regdump > FILE        Needs Mach64Accelerator loaded. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdint.h>
#include "../common/mach64_abi.h"
int main(void)
{
    io_service_t ms = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    io_connect_t mc = 0; vm_address_t ia = 0; vm_size_t iz = 0; volatile unsigned char *io; unsigned r, i;
    if (!ms || IOServiceOpen(ms, mach_task_self(), MACH64_GL_CONNECT_TYPE, &mc) ||
        IOConnectMapMemory(mc, MACH64_MEM_IO, mach_task_self(), &ia, &iz, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "cannot map the I/O window\n"); return 1; }
    io = (volatile unsigned char *)ia;
    for (r = 0; r < 0x100; r += 4) {
        if (r == 0x10 || r == 0x18 || r == 0xc0) continue;
        printf("reg 0x%02x = 0x%08x\n", r, (unsigned)OSReadLittleInt32((void *)io, r));
    }
    io[0xc3] = 0;                                /* DAC_R_INDEX = 0, then read R,G,B for each entry (index auto-increments) */
    for (i = 0; i < 256; i++) { unsigned rr = io[0xc1], gg = io[0xc1], bb = io[0xc1]; printf("pal %3u = %02x %02x %02x\n", i, rr, gg, bb); }
    printf("dac mask 0xc2 = 0x%02x\n", io[0xc2]);
    return 0;
}
