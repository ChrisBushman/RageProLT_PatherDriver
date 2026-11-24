/* mach64_heapdump.c -- read-only walk of Apple's VRAM heap list through the patched kext's guarded peek (GL sel 14), targeted single-address reads only.
 * heap = accel[0x78]; first node = *(*(heap[8])) (alloc_bestfit @0x11584); node = {next, prev, offset, flags(bit31 = free)}.  Stops at a NULL next, a repeat
 * (cycle) or after 400 nodes.  Does not take the kext's mutex, so it works while another thread is stuck inside the allocator. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
static io_connect_t gl;
static int pk(uint32_t a, uint32_t *v) { if (a < 0x10000u || a > 0xdfffffffu || (a & 3u)) { *v = 0; return -1; } return IOConnectMethodScalarIScalarO(gl, 14, 1, 1, 0x80000000u | a, v); }
int main(int argc, char **argv)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    uint32_t accel = 0, heap = 0, a8 = 0, a9 = 0, n = 0, seen[400], i, w[4];
    if (!svc || IOServiceOpen(svc, mach_task_self(), 1, &gl)) { printf("open failed\n"); return 2; }
    IOConnectMethodScalarIScalarO(gl, 14, 1, 1, 0xb0u, &accel);
    pk(accel + 0x78, &heap);
    if (argc > 2) {   /* watch: one compact line per second for argv[1] seconds */
        int secs = atoi(argv[1]), t;
        for (t = 0; t < secs * 20; t++) {
            uint32_t sp = 0, first = 0, nn, k2, w2[4], cnt = 0; pk(heap + 0xc, &sp); 
            { uint32_t x8 = 0, x9 = 0; pk(heap + 8, &x8); pk(x8, &x9); pk(x9, &first); }
            char line[1024]; int ln = 0; static char last[1024]; struct timeval tv; gettimeofday(&tv, 0);
            ln += snprintf(line + ln, sizeof line - ln, "spare=0x%x |", sp);
            for (nn = first; nn && cnt < 12; cnt++) { for (k2 = 0; k2 < 4; k2++) pk(nn + 4 * k2, &w2[k2]); ln += snprintf(line + ln, sizeof line - ln, " %x(n=%x p=%x o=%x f=%x)", nn, w2[0], w2[1], w2[2], w2[3]); nn = w2[0]; }
            if (strcmp(line, last)) { printf("%ld.%03d %s\n", (long)(tv.tv_sec % 1000), (int)(tv.tv_usec / 1000), line); fflush(stdout); strcpy(last, line); }
            usleep(50000);
        }
        return 0;
    }
    if (argc > 1) { printf("connected; dumping in %s s\n", argv[1]); fflush(stdout); sleep(atoi(argv[1])); }   /* keep the connection open while another process triggers the fault */
    pk(accel + 0x78, &heap); printf("accel=0x%x heap=0x%x  heap[0xc](spare pool)=", accel, heap); { uint32_t sp = 0; pk(heap + 0xc, &sp); printf("0x%x\n", sp); }
    pk(heap + 8, &a8); pk(a8, &a9); { uint32_t first = 0; pk(a9, &first);
    printf("heap[8]=0x%x *=0x%x first node=0x%x\n", a8, a9, first); n = first; }
    for (i = 0; n && i < 400; i++) {
        uint32_t j; for (j = 0; j < i; j++) if (seen[j] == n) { printf("CYCLE: node 0x%x already seen at position %u\n", n, j); return 1; }
        seen[i] = n; { int k; for (k = 0; k < 4; k++) pk(n + 4 * k, &w[k]); }
        printf("  [%3u] node 0x%08x next=0x%08x prev=0x%08x off=0x%08x flags=0x%08x %s\n", i, n, w[0], w[1], w[2], w[3], (w[3] & 0x80000000u) ? "FREE" : "used");
        n = w[0];
    }
    printf("walk ended after %u nodes (%s)\n", i, n ? "limit" : "NULL next");
    return 0;
}
