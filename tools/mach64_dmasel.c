/* mach64_dmasel.c -- stage-1 test of the kernel DMA-submit selector (GL sel 7 of the patched ATIRagePro, kext-patch/dma_handler.c).
 * NO hardware access: self-test, argument/stream validation, staging allocation and dry-run descriptor building only.
 *   cc -O2 -o mach64_dmasel mach64_dmasel.c -framework IOKit        run: ./mach64_dmasel
 * Ops: 7 selftest, 0 reset, 1 append (register-write packets), 2 submit (arg bit0 = dry run). */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libkern/OSByteOrder.h>
#include "../common/mach64_abi.h"

#define BADARG 0xe00002c2u
static io_connect_t gl;
static int fails;

static kern_return_t call(uint32_t op, uint32_t arg, const uint32_t *payload, uint32_t nwords, uint32_t *out, uint32_t *outsz)
{
    uint32_t in[2 + 4096]; size_t isz = 8 + 4 * (size_t)nwords, osz = outsz ? *outsz : 0;
    in[0] = op; in[1] = arg; if (nwords) memcpy(in + 2, payload, 4 * (size_t)nwords);
    kern_return_t kr = IOConnectMethodStructureIStructureO(gl, 7, isz, &osz, in, out);
    if (outsz) *outsz = (uint32_t)osz;
    return kr;
}
static void expect(const char *what, kern_return_t got, kern_return_t want)
{
    int ok = (got == want); if (!ok) fails++;
    printf("  %-58s kr=0x%08x  %s\n", what, got, ok ? "ok" : "FAIL (wanted 0x" );
    if (!ok) printf("      wanted 0x%08x\n", want);
    fflush(stdout);
}
#define DMAREG(r) ((r) >= 0x400 ? ((r) - 0x400) >> 2 : (((r) >> 2) | 0x100))

static void mkchunk(uint32_t *w) { int i; for (i = 0; i < 1000; i++) w[i] = (i % 100) ? (uint32_t)i : (DMAREG(0x6c4) | (98u << 16)); }
static void state(const char *tag) { uint32_t o[4] = {0}, sz = 16; kern_return_t kr = call(7, 0, NULL, 0, o, &sz); printf("      state %s: kr=0x%08x magic=0x%08x va=0x%08x pa=0x%08x staged=%u\n", tag, kr, o[0], o[1], o[2], o[3]); }
static kern_return_t sub_dry(uint32_t *out) { uint32_t o = 16; return call(2, 1, NULL, 0, out, &o); }


/* ---- stage 2: one tiny stream on the hardware: a 16x16 solid fill (2D engine, registers all exercised by mach64_bmupload) ---- */
#define DSTBASE 0x300000u
#define PITCH_PX 256u
static int hw_test(void)
{
    io_service_t sv = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    io_connect_t pa; vm_address_t fb = 0; vm_size_t fbs = 0; unsigned x, y, bad = 0, inside = 0, outside = 0;
    uint32_t out[4], osz = 16, sentinel = 0x12345678u, colour = 0xff3366ccu;
    /* register-write packets: {DMAREG(first reg) | (count-1)<<16, values...}; every register below is written the same way by mach64_bmupload (proven) */
    uint32_t s[] = {
        DMAREG(0x6c8), 0xffffffffu,                        /* DP_WRITE_MASK: all bits */
        DMAREG(0x6d0), 0x60060666u,                        /* DP_PIX_WIDTH: ARGB8888 dst/src/host */
        DMAREG(0x6d4), 0x00070007u,                        /* DP_MIX: BKGD_MIX_S | FRGD_MIX_S (copy source) */
        DMAREG(0x6d8), 0x00000101u,                        /* DP_SRC: BKGD/FRGD_SRC_FRGD_CLR (flat colour) */
        DMAREG(0x6c4), colour,                             /* DP_FRGD_CLR */
        DMAREG(0x730), 3u,                                 /* GUI_TRAJ_CNTL: left->right, top->bottom */
        DMAREG(0x6a8), 0x1fff0000u,                        /* scissor (as bmupload) */
        DMAREG(0x6b4), 0x3fff0000u,
        DMAREG(0x708), 0u,                                 /* CLR_CMP_CNTL off */
        DMAREG(0x5fc), 0u,                                 /* SCALE_3D_CNTL = 0: 2D engine only */
        DMAREG(0x500), ((PITCH_PX / 8u) << 22) | (DSTBASE >> 3),   /* DST_OFF_PITCH */
        DMAREG(0x6e8) | (1u << 16), 0u, (16u << 16) | 16u  /* DST_X_Y = 0,0 ; DST_WIDTH_HEIGHT = 16x16 (this write launches the fill) */
    };
    if (!sv || IOServiceOpen(sv, mach_task_self(), MACH64_GL_CONNECT_TYPE, &pa)) { printf("cannot open Mach64Accelerator for the VRAM mapping\n"); return 1; }
    if (IOConnectMapMemory(pa, MACH64_MEM_SURFACE, mach_task_self(), &fb, &fbs, kIOMapAnywhere | kIOMapInhibitCache)) { printf("map VRAM failed\n"); return 1; }
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) OSWriteLittleInt32((void *)fb, DSTBASE + (y * PITCH_PX + x) * 4, sentinel);
    call(0, 0, NULL, 0, NULL, NULL);
    expect("append the fill stream", call(1, 0, s, sizeof s / 4, NULL, NULL), 0);
    expect("submit (REAL: bus-master DMA through the engine)", call(2, 0, NULL, 0, out, &osz), 0);
    printf("      ndesc=%u bytes=%u\n", out[0], out[1]);
    for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) {
        uint32_t got = OSReadLittleInt32((void *)fb, DSTBASE + (y * PITCH_PX + x) * 4), want = (x < 16 && y < 16) ? colour : sentinel;
        if (x < 16 && y < 16) inside += (got == colour); else outside += (got == sentinel);
        if (got != want) { if (bad < 4) printf("      mismatch (%u,%u): got 0x%08x want 0x%08x\n", x, y, got, want); bad++; } }
    printf("      pixels: %u of 256 inside correct, %u of 768 outside untouched\n", inside, outside);
    if (bad) { printf("      FAIL: %u wrong pixels\n", bad); fails++; }
    return 0;
}

int main(int argc, char **argv)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    if (!svc) { printf("no ATIRagePro service\n"); return 2; }
    if (IOServiceOpen(svc, mach_task_self(), 1, &gl) != KERN_SUCCESS) { printf("open GL client failed\n"); return 2; }
    uint32_t out[4], osz;

    if (argc > 1 && !strcmp(argv[1], "hw")) { printf("[hw] stage 2: tiny real DMA stream\n"); hw_test(); printf("%s (%d failure(s))\n", fails ? "FAILED" : "PASSED", fails); return fails != 0; }
    printf("[0] first append on a fresh boot: allocation diagnostics\n");
    { uint32_t pk0[2] = { DMAREG(0x6c4), 0x11223344u }; kern_return_t kr = call(1, 0, pk0, 2, NULL, NULL); expect("first append", kr, 0); state("after first append"); call(0, 0, NULL, 0, NULL, NULL); }
    printf("[1] state block / lock / accelerator pointers\n");
    { uint32_t o[4] = {0}, sz = 16; kern_return_t kr = call(7, 0, NULL, 0, o, &sz);
      expect("selftest op7 arg 0", kr, 0);
      printf("      state: magic=0x%08x staging va=0x%08x pa=0x%08x staged=%u (outsize %u)\n", o[0], o[1], o[2], o[3], sz); }
    expect("selftest op7 arg 0x1234", call(7, 0x1234, NULL, 0, NULL, NULL), 0);
    printf("[2] argument validation\n");
    expect("unknown op 99", call(99, 0, NULL, 0, NULL, NULL), BADARG);
    expect("submit with nothing staged (dry)", sub_dry(out), BADARG);
    expect("append with empty payload", call(1, 0, NULL, 0, NULL, NULL), BADARG);

    printf("[3] staging allocation + dry-run descriptor build\n");
    uint32_t pk[2] = { DMAREG(0x6c4) /*DP_FRGD_CLR*/ | (0u << 16), 0x00ff00ffu };
    expect("append 1 packet (allocates the staging memory)", call(1, 0, pk, 2, NULL, NULL), 0);
    osz = 16; memset(out, 0xee, sizeof out);
    expect("submit dry run", call(2, 1, NULL, 0, out, &osz), 0);
    printf("      ndesc=%u bytes=%u ring_phys=0x%08x data_phys=0x%08x (outsize %u)\n", out[0], out[1], out[2], out[3], osz);
    if (out[0] != 1 || out[1] != 16 || (out[2] & 0x3fff) || out[3] != out[2] + 0x4000 || osz != 16) { printf("      FAIL: unexpected dry-run result\n"); fails++; }
    expect("second dry-run submit (stream was cleared)", sub_dry(out), BADARG);

    printf("[4] large stream: descriptor count / chunking (6 x 4000 bytes + terminator)\n");
    { static uint32_t big[1000]; int i;                                            /* two packets of 999 values per 2000 words */
      mkchunk(big);
      for (i = 0; i < 6; i++) expect("append 4000 bytes (one packet)", call(1, 0, big, 1000, NULL, NULL), 0);
      osz = 16; expect("submit dry run", call(2, 1, NULL, 0, out, &osz), 0);
      printf("      ndesc=%u bytes=%u (expect 6 desc for 24008 bytes)\n", out[0], out[1]);
      if (out[0] != 6 || out[1] != 24008) { printf("      FAIL\n"); fails++; } }

    printf("[5] stream validation refusals (all dry run)\n");
    { uint32_t bad1[2] = { DMAREG(0x6c4) | (3u << 16), 1 };                      /* claims 4 values, has 1 */
      call(0, 0, NULL, 0, NULL, NULL);
      expect("append truncated packet", call(1, 0, bad1, 2, NULL, NULL), 0);
      expect("submit refused (packet overruns the stream)", sub_dry(out), BADARG); }
    { uint32_t bus[2] = { DMAREG(0x4a0) /*BUS_CNTL*/, 0 };
      expect("append write to BUS_CNTL", call(1, 0, bus, 2, NULL, NULL), 0);
      expect("submit refused (denied register)", sub_dry(out), BADARG); }
    { uint32_t src[2] = { DMAREG(0x5b4) /*SRC_CNTL*/, 0 };
      expect("append write to SRC_CNTL", call(1, 0, src, 2, NULL, NULL), 0);
      expect("submit refused (denied register)", sub_dry(out), BADARG); }
    { uint32_t tbl[2] = { DMAREG(0x1b8) /*BM_GUI_TABLE*/, 0 };
      expect("append write to BM_GUI_TABLE", call(1, 0, tbl, 2, NULL, NULL), 0);
      expect("submit refused (denied register)", sub_dry(out), BADARG); }
    { uint32_t odd[3] = { DMAREG(0x6c4), 0, 0 };
      expect("append 12 bytes (valid length) of a 1-value packet + junk", call(1, 0, odd, 3, NULL, NULL), 0);
      expect("submit refused (trailing junk is a bad header)", sub_dry(out), BADARG); }
    printf("[6] overflow: append past 128 KB is refused\n");
    { static uint32_t blk[1000]; int i, n = 0; mkchunk(blk);
      call(0, 0, NULL, 0, NULL, NULL);
      for (i = 0; i < 33; i++) { kern_return_t kr = call(1, 0, blk, 1000, NULL, NULL); if (kr == 0) n++; else { expect("append refused at the limit", kr, BADARG); break; } }
      printf("      %d x 4000 bytes accepted\n", n); if (n != 32) { printf("      FAIL (expected 32)\n"); fails++; }
      call(0, 0, NULL, 0, NULL, NULL); }
    expect("selftest still works", call(7, 5, NULL, 0, NULL, NULL), 0);
    printf("%s (%d failure(s))\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
