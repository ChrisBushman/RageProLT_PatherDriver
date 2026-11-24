/*
 * mach64_probe.c - is this Rage LT Pro initialised?  (headless bring-up probe)
 *
 * Dumps the CRTC / clock / memory / DAC registers, reads the PLL block, estimates VRAM size from
 * MEM_CNTL, and does a VRAM write/readback + aliasing test (offsets < 8MB only; the register window
 * lives at +0x7FF800 and the aperture above 8MB is not proven safe).  VRAM words are restored.
 * Registers are at window + 0x400 + R (R = classic mach64 register offset), little-endian.
 * Needs Mach64Accelerator loaded (tools/probe-test.sh).  Build: -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include "../common/mach64_abi.h"

#define REG_WINDOW 0x7FF800u
static unsigned fb, rw;
static unsigned rd(unsigned r){ return OSReadLittleInt32((void*)rw, 0x400 + r); }
static void     wr(unsigned r, unsigned v){ OSWriteLittleInt32((void*)rw, 0x400 + r, v); __asm__ volatile("eieio" ::: "memory"); }
static unsigned vrd(unsigned o){ return OSReadLittleInt32((void*)fb, o); }
static void     vwr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)fb, o, v); __asm__ volatile("eieio" ::: "memory"); }

static unsigned pll(unsigned idx){            /* PLL read via CLOCK_CNTL: addr in [15:10], data in [23:16] */
    unsigned cc = rd(0x90);
    wr(0x90, (cc & ~0xfe00u) | ((idx & 0x3f) << 10));
    unsigned v = (rd(0x90) >> 16) & 0xff;
    wr(0x90, cc);
    return v;
}

int main(void)
{
    io_service_t svc; io_connect_t conn; vm_address_t addr = 0; vm_size_t size = 0; unsigned i;
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc) { fprintf(stderr, "no Mach64Accelerator\n"); return 1; }
    if (IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn)) { fprintf(stderr, "open\n"); return 1; }
    if (IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), &addr, &size,
                           kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map\n"); return 1; }
    fb = addr; rw = addr + REG_WINDOW;
    printf("aperture %lu bytes @0x%lx\n\n", (unsigned long)size, (unsigned long)addr);

    static const struct { const char *n; unsigned r; } regs[] = {
        {"CRTC_H_TOTAL_DISP",0x00},{"CRTC_H_SYNC_STRT_WID",0x04},{"CRTC_V_TOTAL_DISP",0x08},
        {"CRTC_V_SYNC_STRT_WID",0x0c},{"CRTC_OFF_PITCH",0x14},{"CRTC_GEN_CNTL",0x1c},
        {"DSP_CONFIG",0x20},{"DSP_ON_OFF",0x24},{"MEM_BUF_CNTL",0x2c},{"MEM_ADDR_CONFIG",0x34},
        {"CLOCK_CNTL",0x90},{"CONFIG_STAT1",0x94},{"CONFIG_STAT2",0x98},{"BUS_CNTL",0xa0},
        {"EXT_MEM_CNTL",0xac},{"MEM_CNTL",0xb0},{"DAC_CNTL",0xc4},{"GEN_TEST_CNTL",0xd0},
        {"LCD_GEN_CNTL",0xd4},{"POWER_MANAGEMENT",0xd8},{"CONFIG_CNTL",0xdc},{"CONFIG_CHIP_ID",0xe0},
        {"CONFIG_STAT0",0xe4},{"GUI_STAT",0x338},{"FIFO_STAT",0x310},
    };
    for (i = 0; i < sizeof regs / sizeof regs[0]; i++)
        printf("  %-22s [0x%03x] = 0x%08x\n", regs[i].n, regs[i].r, rd(regs[i].r));

    printf("\nPLL block (via CLOCK_CNTL):\n");
    for (i = 0; i <= 14; i++) printf("  PLL[%2u] = 0x%02x\n", i, pll(i));

    { static const char *ms[] = {"512K","1M","2M","4M","6M","8M","12M","16M"};
      unsigned mc = rd(0xb0); printf("\nMEM_CNTL[2:0]=%u -> VRAM %s (per classic mach64 decode)\n", mc & 7, ms[mc & 7]); }

    printf("\nVRAM write/readback (restored afterwards):\n");
    static const unsigned offs[] = {0x0, 0x1000, 0x100000, 0x200000, 0x400000, 0x600000, 0x7f0000};
    for (i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        unsigned o = offs[i], s = vrd(o), a, b;
        vwr(o, 0xA5C33C5A); a = vrd(o);
        vwr(o, 0x5A3CC3A5); b = vrd(o);
        vwr(o, s);
        printf("  off 0x%06x: saved 0x%08x  wrote A5C33C5A->0x%08x  5A3CC3A5->0x%08x  %s\n",
               o, s, a, b, (a == 0xA5C33C5A && b == 0x5A3CC3A5) ? "OK" : "MISMATCH");
    }
    printf("\nalias test (write marker at 0x0, then at X, see whether 0x0 changed):\n");
    { static const unsigned xs[] = {0x200000, 0x400000, 0x600000, 0x7f0000};
      unsigned s0 = vrd(0), sx, k;
      for (k = 0; k < 4; k++) {
        sx = vrd(xs[k]); vwr(0, 0x11111111); vwr(xs[k], 0x22222222);
        printf("  X=0x%06x: [0]=0x%08x  [X]=0x%08x  %s\n", xs[k], vrd(0), vrd(xs[k]),
               vrd(0) == 0x22222222 ? "ALIAS (wraps here)" : "distinct");
        vwr(xs[k], sx);
      }
      vwr(0, s0); }

    printf("\naddress-pattern sweep: every 64KB across 0..8MB (write all, then verify all):\n");
    { unsigned o, bad = 0, first_bad = 0, good = 0, last_good = 0;
      for (o = 0x7f0000; ; o -= 0x10000) { vwr(o, 0xC0DE0000 ^ o); if (!o) break; }
      for (o = 0; o < 0x800000; o += 0x10000) {
          unsigned v = vrd(o);
          if (v == (0xC0DE0000 ^ o)) { good++; last_good = o; }
          else { if (!bad) first_bad = o; bad++; if (bad <= 6) printf("  off 0x%06x: expected 0x%08x got 0x%08x\n", o, 0xC0DE0000 ^ o, v); }
      }
      printf("  %u/128 slots correct (last good 0x%06x), %u wrong (first wrong 0x%06x)\n", good, last_good, bad, first_bad);
      /* byte-lane / burst check at 0: 64 sequential dwords */
      { unsigned k, errs = 0; for (k = 0; k < 64; k++) vwr(4*k, 0x01010101u * (k+1));
        for (k = 0; k < 64; k++) if (vrd(4*k) != 0x01010101u * (k+1)) errs++;
        printf("  64-dword sequential test at 0: %u errors\n", errs); }
      for (o = 0; o < 0x800000; o += 0x10000) vwr(o, 0); }
    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}
