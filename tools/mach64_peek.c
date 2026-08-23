/*
 * mach64_peek.c - read mach64 registers via the framebuffer aperture's
 * register window.
 *
 * On a PPC/big-endian mach64, the MMIO registers are mapped into the LAST 2KB
 * of the little-endian framebuffer aperture (BAR0 + 8MB - 2KB = +0x7FF800).
 * The 4KB auxiliary aperture (BAR2) is optional and was NOT enabled -> reading
 * it master-aborted (the earlier hangs).  The register window at +0x7FF800 is
 * inside the first 8MB, which we already proved is safely decoded, so these
 * reads cannot master-abort: worst case is a stray pixel value.
 *
 * The window is the little-endian aperture, so register values are stored LE;
 * OSReadLittleInt32 byte-swaps them back on this big-endian CPU.
 *
 * CONFIG_CHIP_ID low 16 bits (CFG_CHIP_TYPE) should be 0x4c49 (Rage LT Pro).
 * Build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define REG_WINDOW      0x7FF800u          /* BAR0 + 8MB - 2KB */
#define CONFIG_CHIP_ID  0x04e0
#define CONFIG_CNTL     0x04dc
#define SCRATCH_REG0    0x0480
#define GUI_STAT        0x0738

int main(void)
{
    io_service_t   svc;
    io_connect_t   conn;
    kern_return_t  kr;
    vm_address_t   addr = 0;
    vm_size_t      size = 0;
    unsigned       i, base, id_le, id_be;

    struct { const char *name; unsigned off; } regs[] = {
        { "CONFIG_CHIP_ID", CONFIG_CHIP_ID },
        { "CONFIG_CNTL",    CONFIG_CNTL    },
        { "SCRATCH_REG0",   SCRATCH_REG0   },
        { "GUI_STAT",       GUI_STAT       },
    };

    svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                      IOServiceMatching("Mach64Accelerator"));
    if (!svc) { fprintf(stderr, "no Mach64Accelerator service\n"); return 1; }
    kr = IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "IOServiceOpen: 0x%x\n", kr); return 1; }

    /* Map the framebuffer BAR0 (type 1); registers are in its top 2KB. */
    kr = IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(),
                            &addr, &size, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOConnectMapMemory: 0x%x\n", kr);
        IOServiceClose(conn); return 1;
    }
    printf("mapped %lu bytes at 0x%lx; register window at +0x%x\n\n",
           (unsigned long)size, (unsigned long)addr, REG_WINDOW);
    if (size < REG_WINDOW + 0x800) {
        fprintf(stderr, "aperture smaller than expected (0x%lx)\n",
                (unsigned long)size);
        IOServiceClose(conn); return 1;
    }
    base = addr + REG_WINDOW;

    printf("about to read register window...\n"); fflush(stdout);
    for (i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        unsigned raw = *(volatile unsigned *)(base + regs[i].off);
        unsigned le  = OSReadLittleInt32((void *)base, regs[i].off);
        printf("  %-14s @win+0x%03x  raw(BE)=0x%08x  LE=0x%08x\n",
               regs[i].name, regs[i].off, raw, le);
        fflush(stdout);
    }

    id_le = OSReadLittleInt32((void *)base, CONFIG_CHIP_ID);
    id_be = *(volatile unsigned *)(base + CONFIG_CHIP_ID);
    printf("\nchip type: LE-low16=0x%04x  BE-low16=0x%04x  (expect 0x4c49)\n",
           id_le & 0xffff, id_be & 0xffff);
    if ((id_le & 0xffff) == 0x4c49)
        printf("CONFIRMED: mach64 registers via LE framebuffer window!\n");
    else if ((id_be & 0xffff) == 0x4c49)
        printf("CONFIRMED: mach64 registers (big-endian read)!\n");
    else
        printf("not 0x4c49 here -- likely VRAM, register window is elsewhere\n");

    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}
