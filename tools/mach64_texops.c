/* mach64_texops.c -- verifies the HARDENED multiplexed new_texture selector (GL sel 10; kext-patch/tex_handler.c).
 * Valid ALLOC / UPLOAD / FREE must work; every malformed or hostile request must be REFUSED (kr != 0) and leave the heap intact.
 * Expected refusals: UPLOAD outside the VRAM aperture (kernel-pointer-sized offsets, wraps, the register window), unaligned, odd or zero length,
 * FREE of a bogus pointer / of an already freed record / of a record twice, AGP ops 3/4/5, unknown ops, short input, undersized output buffer.
 * Prints PASS/FAIL per check and "ALL PASSED". Run after `stubctl notify`; follow with mach64_heapdump to confirm the list is intact. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static io_connect_t gl; static int fails, checks;
static int call(const uint32_t *in, size_t insz, uint32_t *out, size_t outsz)
{ size_t o = outsz; return (int)IOConnectMethodStructureIStructureO(gl, 10, insz, &o, (void *)in, out); }
static void expect(const char *what, int kr, int want_ok)
{ int ok = want_ok ? kr == 0 : kr != 0; checks++; if (!ok) fails++; printf("%s  %-62s kr=0x%x\n", ok ? "PASS" : "FAIL", what, kr); }
static int upload(uint32_t off, uint32_t nbytes)
{ uint32_t buf[2 + 64]; uint32_t i; memset(buf, 0, sizeof buf); buf[0] = 0; buf[1] = off; for (i = 0; i < 64; i++) buf[2 + i] = 0xff000000u | i;
  return call(buf, 8 + nbytes, 0, 0); }
int main(void)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    uint32_t a[2], in[2], out[2], off, rec, off2, rec2, dummy[2];
    if (!svc || IOServiceOpen(svc, mach_task_self(), 1, &gl)) { printf("cannot open GL client\n"); return 2; }
    in[0] = 1; in[1] = 0x2000; expect("ALLOC 0x2000", call(in, 8, a, 8), 1); off = a[0]; rec = a[1];
    printf("      block at 0x%x record 0x%x\n", off, rec);
    expect("UPLOAD 256 B at block start", upload(off, 256), 1);
    expect("UPLOAD 256 B at block end-256", upload(off + 0x2000 - 256, 256), 1);
    expect("UPLOAD 256 B at the very top of usable VRAM (0x7ff700)", upload(0x7ff700u, 256), 1);
    expect("UPLOAD 256 B straddling the register window (0x7ff780)", upload(0x7ff780u, 256), 0);
    expect("UPLOAD unaligned destination", upload(off + 2, 16), 0);
    expect("UPLOAD odd length (10 B)", upload(off, 10), 0);
    expect("UPLOAD zero length", upload(off, 0), 0);
    expect("UPLOAD offset 0xfffffff0 (kernel-pointer-sized)", upload(0xfffffff0u, 16), 0);
    expect("UPLOAD offset 0x80000000", upload(0x80000000u, 16), 0);
    expect("UPLOAD at the register window (0x7ff800)", upload(0x7ff800u, 16), 0);
    expect("UPLOAD offset wrap (0xffffffc0 + 256)", upload(0xffffffc0u, 256), 0);
    { uint32_t b[1] = { 0 }; expect("short input (4 B)", call(b, 4, 0, 0), 0); }
    { uint32_t b[2] = { 9, 0 }; expect("unknown op 9", call(b, 8, dummy, 8), 0); }
    { uint32_t b[2] = { 3, 4096 }; uint32_t o[4]; expect("AGP op 3 refused", call(b, 8, o, 16), 0); }
    { uint32_t b[2] = { 4, 0 }; expect("ALLOC_KEEP size 0 refused", call(b, 8, dummy, 8), 0); }
    { uint32_t b[4] = { 5, 0xdeadbee0u, 0, 1 }; expect("AGP_UPLOAD (op 5) with a bogus handle refused", call(b, 16, 0, 0), 0); }
    in[0] = 1; in[1] = 0; expect("ALLOC size 0 refused", call(in, 8, dummy, 8), 0);
    in[0] = 1; in[1] = 0x1000; expect("ALLOC with a 4-byte output buffer refused", call(in, 8, dummy, 4), 0);
    in[0] = 2; in[1] = 0x12345678u; expect("FREE bogus pointer 0x12345678 refused", call(in, 8, dummy, 8), 0);
    in[0] = 2; in[1] = 0; expect("FREE NULL refused", call(in, 8, dummy, 8), 0);
    in[0] = 2; in[1] = 0xdeadbeefu; expect("FREE 0xdeadbeef refused", call(in, 8, dummy, 8), 0);
    in[0] = 2; in[1] = rec + 4; expect("FREE record+4 (not a record) refused", call(in, 8, dummy, 8), 0);
    in[0] = 1; in[1] = 0x1000; expect("second ALLOC 0x1000", call(in, 8, out, 8), 1); off2 = out[0]; rec2 = out[1];
    expect("UPLOAD into the second block", upload(off2, 64), 1);
    in[0] = 2; in[1] = rec; expect("FREE first record", call(in, 8, dummy, 8), 1);
    in[0] = 2; in[1] = rec; expect("FREE first record again (double free) refused", call(in, 8, dummy, 8), 0);
    in[0] = 2; in[1] = rec2; expect("FREE second record", call(in, 8, dummy, 8), 1);
    in[0] = 1; in[1] = 0x10000000u; { int kr = call(in, 8, out, 8); out[1] = 0; kr = call(in, 8, out, 8); expect("ALLOC 256 MB: refused or no record returned", (kr != 0 || out[1] == 0) ? 0 : 1, 1); }
    in[0] = 1; in[1] = 0x1000; expect("ALLOC 0x1000 after all that", call(in, 8, out, 8), 1);
    in[0] = 2; in[1] = out[1]; expect("FREE it", call(in, 8, dummy, 8), 1);
    /* ownership and reaping: a second client cannot free the first one's record; records are released when their client closes; ALLOC_KEEP survives */
    {   io_connect_t gl2; uint32_t o1[2], o2[2], o3[2], n; io_service_t sv = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
        if (!sv || IOServiceOpen(sv, mach_task_self(), 1, &gl2)) { printf("FAIL  cannot open a second GL client\n"); fails++; checks++; }
        else {
            in[0] = 1; in[1] = 0x1000; expect("client A: ALLOC", call(in, 8, o1, 8), 1);
            { uint32_t f[2] = { 2, o1[1] }; size_t z = 8; kern_return_t kr = IOConnectMethodStructureIStructureO(gl2, 10, 8, &z, f, dummy);
              expect("client B cannot FREE client A's record", (int)kr, 0); }
            in[0] = 1; in[1] = 0x1000; { size_t z = 8; kern_return_t kr = IOConnectMethodStructureIStructureO(gl2, 10, 8, &z, in, o2); expect("client B: its own ALLOC", (int)kr, 1); }
            in[0] = 4; in[1] = 0x1000; expect("client A: ALLOC_KEEP (persistent)", call(in, 8, o3, 8), 1);
            printf("      A owns 0x%x (offset 0x%x), B owns 0x%x (0x%x), persistent 0x%x (0x%x)\n", o1[1], o1[0], o2[1], o2[0], o3[1], o3[0]);
            IOServiceClose(gl2);                                  /* B closes: its record must be released by the kext (clientClose hook) */
            usleep(200000);
            in[0] = 1; in[1] = 0x1000; expect("after B closed: ALLOC reuses B's freed block", (call(in, 8, out, 8) == 0 && out[0] == o2[0]) ? 0 : 1, 1);
            in[0] = 2; in[1] = out[1]; call(in, 8, dummy, 8);
            in[0] = 2; in[1] = o1[1]; expect("client A: FREE its own record", call(in, 8, dummy, 8), 1);
            in[0] = 2; in[1] = o3[1]; expect("client A cannot FREE the persistent record (owner 0)", call(in, 8, dummy, 8), 0);
            (void)n;
        } }
    printf("%s (%d checks, %d failed)\n", fails ? "FAILURES" : "ALL PASSED", checks, fails);
    return fails != 0;
}
