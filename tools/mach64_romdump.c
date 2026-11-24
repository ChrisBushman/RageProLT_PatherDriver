/* mach64_romdump.c - dump the card's PCI expansion ROM (type 6) to a file; report header.
 * usage: mach64_romdump [outfile]   (needs Mach64Accelerator loaded; tools/romdump-test.sh) */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include "../common/mach64_abi.h"
int main(int argc, char **argv)
{
    io_service_t svc; io_connect_t conn; vm_address_t a = 0; vm_size_t sz = 0; unsigned i;
    const char *out = argc > 1 ? argv[1] : "/tmp/rage.rom";
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn)) { fprintf(stderr, "open\n"); return 1; }
    kern_return_t kr = IOConnectMapMemory(conn, MACH64_MEM_ROM, mach_task_self(), &a, &sz, kIOMapAnywhere | kIOMapInhibitCache);
    if (kr) { fprintf(stderr, "map ROM kr=0x%x\n", kr); return 1; }
    printf("ROM mapped: %lu bytes @0x%lx\n", (unsigned long)sz, (unsigned long)a);
    { unsigned char *p = (unsigned char *)a; static unsigned char buf[0x20000];
      unsigned n = sz > sizeof buf ? sizeof buf : (unsigned)sz;
      for (i = 0; i < n; i++) buf[i] = p[i];
      printf("first bytes:"); for (i = 0; i < 16; i++) printf(" %02x", buf[i]); printf("\n");
      printf("signature 55 AA: %s\n", (buf[0] == 0x55 && buf[1] == 0xAA) ? "YES" : "NO");
      FILE *f = fopen(out, "wb"); if (f) { fwrite(buf, 1, n, f); fclose(f); printf("wrote %s (%u bytes)\n", out, n); } }
    IOConnectUnmapMemory(conn, MACH64_MEM_ROM, mach_task_self(), a);
    IOServiceClose(conn);
    return 0;
}
