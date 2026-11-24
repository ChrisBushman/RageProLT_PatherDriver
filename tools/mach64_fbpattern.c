/*
 * mach64_fbpattern.c - draw a test pattern into the scanout buffer at VRAM offset 0.
 *
 * Assumes the display was set to the BIOS's VBE mode 0112 (640x480, 24 bpp packed, pitch 1920 bytes,
 * CRTC_OFF_PITCH 0x14000000 = offset 0, CRTC_PIX_WIDTH 5 = 8,8,8): see docs/re-notes/rage-bios-vbe-modeset.md.
 * It first reads CRTC_OFF_PITCH / CRTC_GEN_CNTL and refuses to draw if they do not match.
 * Byte stores only (no CPU-endianness dependence); packed pixel = B, G, R in increasing address order.
 * Needs Mach64Accelerator loaded.  Usage: mach64_fbpattern [bars|clear|ramp]
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include "../common/mach64_abi.h"

#define W 640
#define H 480
#define PITCH (W * 3)

static unsigned char *fb;
static void pix(int x, int y, unsigned r, unsigned g, unsigned b)
{
    unsigned char *p = fb + (unsigned)y * PITCH + (unsigned)x * 3;
    p[0] = b; p[1] = g; p[2] = r;
}

int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; kern_return_t kr; vm_address_t addr = 0; vm_size_t size = 0;
    vm_address_t ioa = 0; vm_size_t ios = 0; unsigned regs, offp, gen, x, y;
    const char *mode = argc > 1 ? argv[1] : "bars";
    static const unsigned bar[8][3] = { {255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0} };

    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc) { fprintf(stderr, "no Mach64Accelerator service\n"); return 1; }
    kr = IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn); IOObjectRelease(svc);
    if (kr) { fprintf(stderr, "IOServiceOpen 0x%x\n", kr); return 1; }
    kr = IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), &addr, &size, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr) { fprintf(stderr, "map 0x%x\n", kr); return 1; }
    kr = IOConnectMapMemory(conn, MACH64_MEM_IO, mach_task_self(), &ioa, &ios, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr) { fprintf(stderr, "map I/O window 0x%x\n", kr); return 1; }
    fb = (unsigned char *)addr; regs = ioa;
    offp = OSReadLittleInt32((void *)regs, 0x14); gen = OSReadLittleInt32((void *)regs, 0x1c);
    printf("aperture %p size 0x%lx  CRTC_OFF_PITCH 0x%08x CRTC_GEN_CNTL 0x%08x\n", (void *)addr, (unsigned long)size, offp, gen);
    if (offp != 0x14000000 || ((gen >> 8) & 7) != 5 || !(gen & 0x02000000)) { fprintf(stderr, "display is not the expected 640x480x24 mode; refusing\n"); return 2; }

    if (!strcmp(mode, "clear")) { for (y = 0; y < H; y++) for (x = 0; x < W; x++) pix(x, y, 0, 0, 0); printf("cleared\n"); return 0; }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned r, g, b;
            if (!strcmp(mode, "ramp") || y >= 320) { unsigned v = x * 255 / (W - 1); r = g = b = v; }
            else { const unsigned *c = bar[x / 80]; r = c[0]; g = c[1]; b = c[2]; }
            if (x < 4 || x >= W - 4 || y < 4 || y >= H - 4) r = g = b = 255;
            if (x >= 8 && x < 40 && y >= 8 && y < 40) { r = 255; g = 0; b = 0; }          /* top-left: red */
            if (x >= W - 40 && x < W - 8 && y >= H - 40 && y < H - 8) { r = 0; g = 0; b = 255; } /* bottom-right: blue */
            pix(x, y, r, g, b);
        }
    printf("pattern '%s' drawn (%dx%d, 24bpp packed B,G,R)\n", mode, W, H);
    return 0;
}
