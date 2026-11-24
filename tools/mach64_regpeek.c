/* mach64_regpeek.c - read-only dump of a fixed list of status/config registers through the Mach64Accelerator BAR0 mapping.
 * Registers live in the top 2 KB of BAR0 (BAR0 + 0x7FF800 + real DRI offset).  Reads only; the list is fixed in the source and none of
 * these registers has a read side effect.   sudo ./mach64_regpeek */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdint.h>
#include "../common/mach64_abi.h"
static const struct { unsigned off; const char *name; } R[] = {
    { 0x414, "CRTC_OFF_PITCH" }, { 0x41c, "CRTC_GEN_CNTL" }, { 0x4a0, "BUS_CNTL" }, { 0x4b0, "MEM_CNTL" }, { 0x4dc, "CONFIG_CNTL" },
    { 0x710, "FIFO_STAT" }, { 0x738, "GUI_STAT" },
};
int main(void)
{
    io_service_t ms = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    io_connect_t mc = 0; vm_address_t a = 0; vm_size_t sz = 0; unsigned i;
    if (!ms || IOServiceOpen(ms, mach_task_self(), MACH64_GL_CONNECT_TYPE, &mc) ||
        IOConnectMapMemory(mc, MACH64_MEM_SURFACE, mach_task_self(), &a, &sz, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "cannot map BAR0\n"); return 1; }
    printf("BAR0 mapped, size 0x%lx\n", (unsigned long)sz);
    if (sz < 0x800000) { fprintf(stderr, "mapping too small\n"); return 1; }
    for (i = 0; i < sizeof R / sizeof R[0]; i++)
        printf("%-16s [0x%03x] = 0x%08x\n", R[i].name, R[i].off, (unsigned)OSReadLittleInt32((void *)(a + 0x7ff800u), R[i].off));
    return 0;
}
