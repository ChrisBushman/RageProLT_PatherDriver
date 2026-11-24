/* vramdump.c: read-only dump of a VRAM byte range through the accelerator's surface mapping.  usage: vramdump OFF LEN FILE (hex or dec) */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../common/mach64_abi.h"
int main(int argc, char **argv)
{
    unsigned long off, len, i; FILE *f;
    io_service_t ms; io_connect_t mc = 0; vm_address_t fa = 0; vm_size_t fz = 0;
    if (argc < 4) { fprintf(stderr, "usage: %s OFF LEN FILE\n", argv[0]); return 2; }
    off = strtoul(argv[1], 0, 0); len = strtoul(argv[2], 0, 0);
    ms = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!ms || IOServiceOpen(ms, mach_task_self(), MACH64_GL_CONNECT_TYPE, &mc) ||
        IOConnectMapMemory(mc, MACH64_MEM_SURFACE, mach_task_self(), &fa, &fz, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "cannot map VRAM\n"); return 1; }
    fprintf(stderr, "mapped size 0x%lx\n", (unsigned long)fz);
    if (off + len > fz || off + len > 0x7f0000) { fprintf(stderr, "range out of mapping\n"); return 1; }
    f = fopen(argv[3], "wb"); if (!f) { perror(argv[3]); return 1; }
    for (i = 0; i < len; i++) fputc(((volatile unsigned char *)fa)[off + i], f);
    fclose(f); printf("dumped 0x%lx+0x%lx -> %s\n", off, len, argv[3]); return 0;
}
