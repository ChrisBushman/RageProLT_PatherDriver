/* mach64_allocfail.c -- does a FAILED sel-10 ALLOC hand back an uninitialised, never-linked heap record that the plugin then FREEs?
 * Static analysis (kext-patch/handler_mux.py + stock heap_free 0x118dc): ALLOC does record=IOMallocPageable(0x10), sets only record[8]=0, calls
 * alloc_bestfit; on failure the record is NOT linked into the heap, yet is returned in out[1].  gld_render.c mach64_vram_alloc's reject path then sends
 * FREE(out[1]) -> heap_free reads record[0]/[4] (never written) as list neighbours and writes through them.
 *   ./mach64_allocfail          : oversized ALLOC, print the returned record and its four words (read-only peek)
 *   ./mach64_allocfail go       : ... then send the FREE exactly as the plugin does (expected: kernel fault or heap corruption) */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static io_connect_t gl;
static int sel10(uint32_t op, uint32_t arg, uint32_t *out)
{
    uint32_t in[2] = { op, arg }; size_t osz = 8; kern_return_t kr = IOConnectMethodStructureIStructureO(gl, 10, 8, &osz, in, out);
    return (int)kr;
}
static int peek(uint32_t kaddr, uint32_t *v)
{
    if (kaddr < 0x10000u || kaddr > 0xdfffffffu || (kaddr & 3u)) return -1;
    return IOConnectMethodScalarIScalarO(gl, 14, 1, 1, 0x80000000u | kaddr, v);
}
int main(int argc, char **argv)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    uint32_t out[2] = {0, 0}, w[4] = {0}, i; int go = argc > 1 && !strcmp(argv[1], "go");
    if (!svc || IOServiceOpen(svc, mach_task_self(), 1, &gl)) { printf("cannot open GL client\n"); return 2; }
    int kr = sel10(1, 0x10000000u, out);   /* 256 MB: cannot fit in an 8 MB VRAM heap */
    printf("ALLOC 256MB: kr=0x%x out[0]=0x%x (offset; 0 = failed)  out[1]=0x%x (record)\n", kr, out[0], out[1]);
    if (!out[1]) { printf("no record returned: nothing to analyse\n"); return 0; }
    for (i = 0; i < 4; i++) { int r = peek(out[1] + 4 * i, &w[i]); printf("  record[0x%x] = 0x%08x (peek kr=%d)\n", 4 * i, w[i], r); }
    printf("record[8]=%u is the only field the kernel initialised; [0] next, [4] prev, [0xc] flags/size are whatever the pageable allocation held.\n", w[2]);
    if (!go) { printf("not sending FREE (run with 'go')\n"); return 0; }
    printf("sending FREE(record) as the plugin's reject path does...\n"); fflush(stdout);
    kr = sel10(2, out[1], out);
    printf("FREE returned kr=0x%x (still alive)\n", kr);
    return 0;
}
