/*
 * mach64_poke.c - first register WRITE: round-trip a scratch register.
 *
 * SCRATCH_REG1 is pure software storage (no hardware side effects), so this is
 * safe even while ATIRagePro drives the display.  We save the original value,
 * write two distinct patterns and read each back to prove writes stick, then
 * restore the original.
 *
 * Registers live in the little-endian framebuffer window (BAR0 + 0x7FF800),
 * so use OSWrite/ReadLittleInt32 (byte-swap on this big-endian CPU).
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define REG_WINDOW    0x7FF800u
#define SCRATCH_REG1  0x0484

static int check(const char *label, unsigned base, unsigned want)
{
    unsigned got;
    OSWriteLittleInt32((void *)base, SCRATCH_REG1, want);
    got = OSReadLittleInt32((void *)base, SCRATCH_REG1);
    printf("  wrote 0x%08x -> read 0x%08x  %s\n", want, got,
           (got == want) ? "OK" : "MISMATCH");
    return got == want;
}

int main(void)
{
    io_service_t  svc;
    io_connect_t  conn;
    kern_return_t kr;
    vm_address_t  addr = 0;
    vm_size_t     size = 0;
    unsigned      base, orig, ok = 1;

    svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                      IOServiceMatching("Mach64Accelerator"));
    if (!svc) { fprintf(stderr, "no Mach64Accelerator service\n"); return 1; }
    kr = IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "IOServiceOpen: 0x%x\n", kr); return 1; }
    kr = IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(),
                            &addr, &size, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "map: 0x%x\n", kr); IOServiceClose(conn); return 1; }
    base = addr + REG_WINDOW;

    orig = OSReadLittleInt32((void *)base, SCRATCH_REG1);
    printf("SCRATCH_REG1 original = 0x%08x\n", orig);
    printf("writing test patterns...\n"); fflush(stdout);

    ok &= check("pattern1", base, 0xdeadbeef);
    ok &= check("pattern2", base, 0x12345678);
    ok &= check("zero",     base, 0x00000000);

    /* restore */
    OSWriteLittleInt32((void *)base, SCRATCH_REG1, orig);
    printf("restored SCRATCH_REG1 = 0x%08x\n",
           OSReadLittleInt32((void *)base, SCRATCH_REG1));

    printf("\n%s\n", ok ? "REGISTER WRITE CONFIRMED: read+write both work!"
                        : "*** write did not round-trip ***");

    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return ok ? 0 : 1;
}
