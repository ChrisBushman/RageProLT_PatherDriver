/*
 * mach64_peek.c - first MMIO read: open the Mach64Accelerator user client, map
 * the register aperture (MACH64_MEM_REGISTERS), and read CONFIG_CHIP_ID.
 *
 * Its low 16 bits (CFG_CHIP_TYPE) should be 0x4c49 for the Rage LT Pro 'LI',
 * which proves we mapped the real registers.  Reads are side-effect-free, so
 * this is safe to run while ATIRagePro is driving the display.
 *
 * Reads each register both native (big-endian) and little-endian, because the
 * mach64 register file is little-endian and we don't yet know whether this
 * Mac aperture is pre-swapped.  Userspace tool; build with -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

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
    unsigned       i, id_le, id_be;

    struct { const char *name; unsigned off; } regs[] = {
        { "CONFIG_CHIP_ID", CONFIG_CHIP_ID },
        { "CONFIG_CNTL",    CONFIG_CNTL    },
        { "SCRATCH_REG0",   SCRATCH_REG0   },
        { "GUI_STAT",       GUI_STAT       },
    };

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
    kr = IOConnectMapMemory(conn, MACH64_MEM_REGISTERS, mach_task_self(),
                            &addr, &size, kIOMapAnywhere);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOConnectMapMemory(registers) failed: 0x%x\n", kr);
        IOServiceClose(conn);
        return 1;
    }
    printf("mapped %lu bytes of registers at 0x%lx\n\n",
           (unsigned long)size, (unsigned long)addr);

    for (i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        unsigned raw = *(volatile unsigned *)(addr + regs[i].off);
        unsigned le  = OSReadLittleInt32((void *)addr, regs[i].off);
        printf("%-14s @0x%03x  raw(BE)=0x%08x  LE=0x%08x\n",
               regs[i].name, regs[i].off, raw, le);
    }

    id_le = OSReadLittleInt32((void *)addr, CONFIG_CHIP_ID);
    id_be = *(volatile unsigned *)(addr + CONFIG_CHIP_ID);
    printf("\nchip type: LE-low16=0x%04x  BE-low16=0x%04x  (expect 0x4c49)\n",
           id_le & 0xffff, id_be & 0xffff);
    if ((id_le & 0xffff) == 0x4c49)
        printf("CONFIRMED: real mach64 registers, little-endian access\n");
    else if ((id_be & 0xffff) == 0x4c49)
        printf("CONFIRMED: real mach64 registers, big-endian (pre-swapped) access\n");
    else
        printf("neither matches 0x4c49 - aperture/offset needs review\n");

    IOConnectUnmapMemory(conn, MACH64_MEM_REGISTERS, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}
