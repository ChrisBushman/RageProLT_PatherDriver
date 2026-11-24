/* mach64_hold.c - test Path A selectors 17/18 (hold/release Apple's accelerator mutex across calls).  usage: mach64_hold SECONDS
 * (holds, sleeps, releases; kill -9 it to test the dying-client release).  The mutex address comes from Apple's own GL context via its patched peek selector (14). */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../common/mach64_abi.h"
static int peek(io_connect_t c, uint32_t kaddr, uint32_t *v) { return IOConnectMethodScalarIScalarO(c, 14, 1, 1, 0x80000000u | kaddr, v); }
int main(int argc, char **argv)
{
    io_service_t svc, asvc; io_connect_t conn, ac; uint32_t accel = 0, mtx = 0; kern_return_t kr; int secs = argc > 1 ? atoi(argv[1]) : 3;
    setvbuf(stdout, 0, _IONBF, 0);
    asvc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    if (!asvc || IOServiceOpen(asvc, mach_task_self(), 1, &ac)) { printf("open ATIRagePro failed\n"); return 1; }
    IOConnectMethodScalarIScalarO(ac, 14, 1, 1, 0xb0u, &accel);
    if (peek(ac, accel + 0x544u, &mtx)) { printf("peek failed (accel 0x%x)\n", accel); return 1; }
    printf("accel=0x%08x mutex=0x%08x\n", accel, mtx);
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn)) { printf("open Path A failed\n"); return 1; }
    kr = IOConnectMethodScalarIScalarO(conn, kMach64Sel_accel_hold, 1, 0, mtx);
    printf("hold: kr=0x%x\n", (unsigned)kr);
    if (kr) return 1;
    sleep(secs);
    kr = IOConnectMethodScalarIScalarO(conn, kMach64Sel_accel_release, 0, 0);
    printf("release: kr=0x%x\n", (unsigned)kr);
    return 0;
}
