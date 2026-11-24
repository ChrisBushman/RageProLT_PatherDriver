/*
 * mach64_blank.c - set/clear CRTC_GEN_CNTL.CRTC_DISPLAY_DIS (bit 6, "forces the blanking signal active").
 * After the BIOS VBE mode set (docs/re-notes/rage-bios-vbe-modeset.md) the bit is left set: sync runs but the screen is black.
 * Read-modify-write of that single bit, with readback.  Usage: mach64_blank off|on     (off = display visible)
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include "../common/mach64_abi.h"
#define CRTC_GEN_CNTL 0x1c
#define CRTC_DISPLAY_DIS (1u << 6)
int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; vm_address_t a = 0; vm_size_t z = 0; unsigned v, nv;
    int blank = (argc > 1 && !strcmp(argv[1], "on"));
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn)) { fprintf(stderr, "no Mach64Accelerator\n"); return 1; }
    if (IOConnectMapMemory(conn, MACH64_MEM_IO, mach_task_self(), &a, &z, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map I/O window failed\n"); return 1; }
    v = OSReadLittleInt32((void *)a, CRTC_GEN_CNTL);
    if (!(v & 0x02000000)) { fprintf(stderr, "CRTC not enabled (0x%08x); refusing\n", v); return 2; }
    nv = blank ? (v | CRTC_DISPLAY_DIS) : (v & ~CRTC_DISPLAY_DIS);
    OSWriteLittleInt32((void *)a, CRTC_GEN_CNTL, nv);
    printf("CRTC_GEN_CNTL 0x%08x -> 0x%08x (readback 0x%08x)\n", v, nv, OSReadLittleInt32((void *)a, CRTC_GEN_CNTL));
    return 0;
}
