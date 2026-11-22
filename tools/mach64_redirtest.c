/* mach64_redirtest.c -- does a user mapping of Mach64Accelerator's aperture survive a display mode change?
 *
 * Maps MACH64_MEM_SURFACE (BAR0: VRAM + register window) through the GL user client, then reads GUI_STAT and the kext's mode
 * generation every 200 ms, printing one line per second (flushed, also to /tmp/redirtest.log).  Run it, switch resolution with
 * mach64_modeswitch from another shell, and watch whether the reads keep going after "did change".  A read that never returns
 * means the redirected mapping is not restored (the process will be stuck in the kernel: reboot).
 *
 *   cc -o mach64_redirtest mach64_redirtest.c -framework IOKit
 *   ./mach64_redirtest [seconds]          (default 60)
 */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../common/mach64_abi.h"
#include "../common/mach64_regs.h"

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 60, i;
    io_service_t sv = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    io_connect_t c;
    vm_address_t a = 0; vm_size_t sz = 0;
    FILE *log = fopen("/tmp/redirtest.log", "w");
    if (!sv || IOServiceOpen(sv, mach_task_self(), MACH64_GL_CONNECT_TYPE, &c)) { fprintf(stderr, "open failed\n"); return 1; }
    if (IOConnectMapMemory(c, MACH64_MEM_SURFACE, mach_task_self(), &a, &sz, kIOMapAnywhere | kIOMapInhibitCache) || !a) {
        fprintf(stderr, "map failed\n"); return 1; }
    printf("aperture mapped at 0x%lx, %lu bytes\n", (unsigned long)a, (unsigned long)sz); fflush(stdout);
    for (i = 0; i < secs * 5; i++) {
        uint32_t gen = 0, st;
        IOConnectMethodScalarIScalarO(c, kMach64Sel_get_mode_gen, 0, 1, &gen);
        if (i % 5 == 0) { printf("t=%ds gen=%u reading...", i / 5, gen); fflush(stdout); if (log) { fprintf(log, "t=%ds gen=%u reading\n", i / 5, gen); fflush(log); fsync(fileno(log)); } }
        st = OSReadLittleInt32((void *)(a + MACH64_REG_WINDOW), MACH64_GUI_STAT);   /* user access through the (possibly redirected) mapping */
        if (i % 5 == 0) { printf(" GUI_STAT=0x%08x\n", st); fflush(stdout); if (log) { fprintf(log, "  ok 0x%08x\n", st); fflush(log); fsync(fileno(log)); } }
        usleep(200000);
    }
    IOConnectUnmapMemory(c, MACH64_MEM_SURFACE, mach_task_self(), a);
    IOServiceClose(c);
    printf("done\n");
    return 0;
}
