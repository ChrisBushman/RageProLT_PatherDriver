/* mach64_scanshot.c - snapshot and compare the Rage scanout (VRAM offset 0, 640x480, 15 bpp 1:5:5:5 little-endian in the unswapped lower view).
 * Used to find WHERE stale pixels sit after a GL window closes: save a shot before and after, diff against a repainted desktop.
 *   sudo ./mach64_scanshot save FILE          dump the scanout (614400 bytes)
 *   ./mach64_scanshot diff A B [PGM]          list differing 32x32 tiles, bounding box, pixel count; optional PGM of the diff mask
 * Needs Mach64Accelerator loaded (type-7 aperture mapping).  Reads only. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../common/mach64_abi.h"
#define W 640
#define H 480
int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "save")) {
        io_service_t ms = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
        io_connect_t mc = 0; vm_address_t fa = 0; vm_size_t fz = 0; FILE *f; int y;
        if (!ms || IOServiceOpen(ms, mach_task_self(), MACH64_GL_CONNECT_TYPE, &mc) ||
            IOConnectMapMemory(mc, MACH64_MEM_SURFACE, mach_task_self(), &fa, &fz, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "cannot map VRAM\n"); return 1; }
        f = fopen(argv[2], "wb"); if (!f) { perror(argv[2]); return 1; }
        for (y = 0; y < H; y++) { unsigned char row[W * 2]; int x;   /* byte-wise reads: the mapping is uncached big-endian CPU view of LE VRAM */
            for (x = 0; x < W * 2; x++) row[x] = ((volatile unsigned char *)fa)[y * W * 2 + x];
            fwrite(row, 1, sizeof row, f); }
        fclose(f); printf("saved %s (%d bytes)\n", argv[2], W * H * 2); return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "diff")) {
        static unsigned char a[W * H * 2], b[W * H * 2]; static int tiles[(H / 32 + 1) * (W / 32)];
        FILE *fa = fopen(argv[2], "rb"), *fb = fopen(argv[3], "rb"); int x, y, n = 0, x0 = W, y0 = H, x1 = -1, y1 = -1, tx, ty; FILE *pg = argc > 4 ? fopen(argv[4], "wb") : 0;
        if (!fa || !fb || fread(a, 1, sizeof a, fa) != sizeof a || fread(b, 1, sizeof b, fb) != sizeof b) { fprintf(stderr, "bad shot files\n"); return 1; }
        if (pg) fprintf(pg, "P5\n%d %d\n255\n", W, H);
        for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
            int d = memcmp(&a[(y * W + x) * 2], &b[(y * W + x) * 2], 2) != 0;
            if (d) { n++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; tiles[(y / 32) * (W / 32) + x / 32]++; }
            if (pg) fputc(d ? 255 : 0, pg);
        }
        printf("%d of %d pixels differ", n, W * H);
        if (n) printf("; bbox x %d..%d y %d..%d (%dx%d)", x0, x1, y0, y1, x1 - x0 + 1, y1 - y0 + 1);
        printf("\n");
        for (ty = 0; ty < H / 32; ty++) { for (tx = 0; tx < W / 32; tx++) { int c = tiles[ty * (W / 32) + tx]; putchar(c == 0 ? '.' : c < 100 ? '+' : '#'); } putchar('\n'); }
        if (pg) fclose(pg);
        return 0;
    }
    fprintf(stderr, "usage: %s save FILE | diff A B [PGM]\n", argv[0]);
    return 2;
}
