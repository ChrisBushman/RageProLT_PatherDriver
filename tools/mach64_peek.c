/*
 * mach64_peek.c - SAFE isolation test: map the FRAMEBUFFER (VRAM) aperture and
 * read a few dwords.
 *
 * Why VRAM and not registers: VRAM is fully-decoded memory, so a read can't
 * master-abort / machine-check the way a stray register-offset read can.  This
 * isolates one question: does userspace IOConnectMapMemory of a device BAR +
 * CPU read work at all on this setup?
 *   - reads succeed  -> mapping path is sound; the earlier hang was the
 *                       register offset/aperture, which we tackle next.
 *   - hangs          -> userspace device mapping itself is the problem; we
 *                       switch to in-kernel register reads via a method call.
 *
 * Reads are side-effect-free (VRAM the display driver already wrote), so this
 * is safe alongside ATIRagePro.  Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

int main(void)
{
    io_service_t   svc;
    io_connect_t   conn;
    kern_return_t  kr;
    vm_address_t   addr = 0;
    vm_size_t      size = 0;
    unsigned       i;

    svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                      IOServiceMatching("Mach64Accelerator"));
    if (!svc) {
        fprintf(stderr, "no Mach64Accelerator service (is the kext loaded?)\n");
        return 1;
    }
    kr = IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceOpen failed: 0x%x\n", kr);
        return 1;
    }

    /* Map the framebuffer/VRAM aperture (type 1).  Cache-inhibited. */
    kr = IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(),
                            &addr, &size, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOConnectMapMemory(framebuffer) failed: 0x%x\n", kr);
        IOServiceClose(conn);
        return 1;
    }
    printf("mapped %lu bytes of VRAM at 0x%lx\n", (unsigned long)size,
           (unsigned long)addr);
    printf("about to read first dword...\n");
    fflush(stdout);

    /* Read 8 dwords of VRAM.  Non-zero/varied => real framebuffer content. */
    for (i = 0; i < 8; i++) {
        unsigned v = *(volatile unsigned *)(addr + i * 4);
        printf("  VRAM[0x%02x] = 0x%08x\n", i * 4, v);
        fflush(stdout);
    }
    /* A sample deeper in, still safely inside the 16MB aperture. */
    if (size > 0x100000) {
        unsigned v = *(volatile unsigned *)(addr + 0x100000);
        printf("  VRAM[0x100000] = 0x%08x\n", v);
    }
    printf("VRAM read OK -- userspace device mapping works.\n");

    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}
