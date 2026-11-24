/*
 * mach64_post.c - replay the ATI BIOS init sequence (tools/mach64_post_seq.h) on a card the Mac's OpenFirmware never POSTed,
 * then re-run the VRAM address-pattern sweep.  usage: mach64_post [full]   (default = PLL + memory/bus registers only)
 * Needs Mach64Accelerator loaded (tools/post-test.sh).  Build: -framework IOKit.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include "../common/mach64_abi.h"
#include "mach64_post_seq.h"
#include "mach64_bios_seq.h"

#define REG_WINDOW 0x7FF800u
static unsigned fb, rw;
static unsigned rd(unsigned r){ return OSReadLittleInt32((void*)rw, 0x400 + r); }
static void wr(unsigned r, unsigned v){ OSWriteLittleInt32((void*)rw, 0x400 + r, v); __asm__ volatile("eieio" ::: "memory"); }
static unsigned vrd(unsigned o){ return OSReadLittleInt32((void*)fb, o); }
static void vwr(unsigned o, unsigned v){ OSWriteLittleInt32((void*)fb, o, v); __asm__ volatile("eieio" ::: "memory"); }

static void sweep(void)
{
    unsigned o, good = 0, bad = 0, first_bad = 0, k, errs = 0, last_good = 0;
    for (o = 0x7f0000; ; o -= 0x10000) { vwr(o, 0xC0DE0000 ^ o); if (!o) break; }
    for (o = 0; o < 0x800000; o += 0x10000) {
        unsigned v = vrd(o);
        if (v == (0xC0DE0000 ^ o)) { good++; last_good = o; } else { if (!bad) first_bad = o; bad++; }
    }
    printf("  sweep: %u/128 64KB-slots correct (last good 0x%06x), %u wrong (first wrong 0x%06x)\n", good, last_good, bad, first_bad);
    for (k = 0; k < 64; k++) vwr(4*k, 0x01010101u * (k+1));
    for (k = 0; k < 64; k++) if (vrd(4*k) != 0x01010101u * (k+1)) errs++;
    printf("  64-dword sequential test at 0: %u errors\n", errs);
    for (o = 0; o < 0x800000; o += 0x10000) vwr(o, 0);
}


static int usock = -1; static struct sockaddr_in uadr;
static void ulog(const char *m){ if (usock >= 0) sendto(usock, m, strlen(m), 0, (struct sockaddr*)&uadr, sizeof uadr); }
int main(int argc, char **argv)
{
    /* usage: mach64_post [full] [steps=N] [only=pll|mem] [sweep]
     *   steps=N  replay only the first N steps (bisecting a hang);  only=pll  skip non-PLL steps;  sweep  run the VRAM sweep
     * Every step is logged + fsync'd to ~/post-progress.log BEFORE it executes: after a hard lock-up the last line is the culprit. */
    io_service_t svc; io_connect_t conn; vm_address_t addr = 0; vm_size_t size = 0;
    unsigned i, full = 0, maxsteps = 0xffffffffu, only_pll = 0, do_sweep = 0, nsteps = sizeof post_seq / sizeof post_seq[0];
    FILE *lg; char path[256]; unsigned have_sel = 0, do_vram1 = 0, do_sdram = 0, use_bar2 = 0, use_io = 0, do_dump = 0, do_bios = 0, nocrtc = 0, skip_a = 1, skip_b = 0, noread = 0x7fffffff, pllwait = 0; unsigned char sel[256]; memset(sel, 0, sizeof sel);
    for (i = 1; i < (unsigned)argc; i++) {
        if (!strcmp(argv[i], "full")) full = 1;
        else if (!strncmp(argv[i], "steps=", 6)) maxsteps = atoi(argv[i] + 6);
        else if (!strcmp(argv[i], "only=pll")) only_pll = 1;
        else if (!strcmp(argv[i], "sweep")) do_sweep = 1;
        else if (!strcmp(argv[i], "vram1")) do_vram1 = 1;
        else if (!strcmp(argv[i], "sdram")) do_sdram = 1;
        else if (!strcmp(argv[i], "bar2")) use_bar2 = 1;
        else if (!strcmp(argv[i], "io")) use_io = 1;
        else if (!strcmp(argv[i], "dump")) do_dump = 1;
        else if (!strcmp(argv[i], "bios")) do_bios = 1;
        else if (!strcmp(argv[i], "nocrtc")) nocrtc = 1;
        else if (!strncmp(argv[i], "skip=", 5)) { skip_a = atoi(argv[i] + 5); skip_b = atoi(strchr(argv[i], '-') + 1); }
        else if (!strncmp(argv[i], "noread=", 7)) noread = atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "pllwait=", 8)) pllwait = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "sel=", 4)) { char *p = argv[i] + 4; have_sel = 1; while (*p) { unsigned n = strtoul(p, &p, 10); if (n < 256) sel[n] = 1; if (*p == ',') p++; else break; } }
    }
    snprintf(path, sizeof path, "%s/post-progress.log", getenv("HOME") ? getenv("HOME") : "/tmp");
    lg = fopen(path, "w");
    if (getenv("UDPLOG")) { usock = socket(AF_INET, SOCK_DGRAM, 0); memset(&uadr, 0, sizeof uadr); uadr.sin_family = AF_INET; uadr.sin_port = htons(9999); uadr.sin_addr.s_addr = inet_addr(getenv("UDPLOG")); ulog("post: start"); }
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn)) { fprintf(stderr, "open\n"); return 1; }
    if (IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), &addr, &size, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map\n"); return 1; }
    fb = addr; rw = addr + REG_WINDOW;
    if (use_io) { vm_address_t a3 = 0; vm_size_t s3 = 0;   /* BAR1 I/O window: dense, register R at +R; rd/wr add 0x400, so bias the base */
        if (IOConnectMapMemory(conn, 7, mach_task_self(), &a3, &s3, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map io\n"); return 1; }
        rw = a3 - 0x400; printf("registers via BAR1 I/O window at %p size 0x%x, CHIP_ID=0x%08x\n", (void*)a3, (unsigned)s3, rd(0xe0)); fflush(stdout); }
    if (use_bar2) { vm_address_t a2 = 0; vm_size_t s2 = 0;
        if (IOConnectMapMemory(conn, MACH64_MEM_REGISTERS, mach_task_self(), &a2, &s2, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map bar2\n"); return 1; }
        rw = a2; printf("registers via BAR2 at %p size 0x%x, CHIP_ID=0x%08x\n", (void*)a2, (unsigned)s2, rd(0xe0)); fflush(stdout); }
    printf("before: MEM_CNTL=0x%08x EXT_MEM_CNTL=0x%08x CLOCK_CNTL=0x%08x\n", rd(0xb0), rd(0xac), rd(0x90)); fflush(stdout);
    printf("replaying up to %u of %u steps (%s%s), log %s\n", maxsteps < nsteps ? maxsteps : nsteps, nsteps,
           full ? "full" : "pll+memory/bus", only_pll ? ", PLL only" : "", path); fflush(stdout);
    if (do_dump) { static const unsigned rl[] = { 0x18,0x1c,0x28,0x2c,0x34,0x4c,0x50,0x74,0x78,0x7c,0x80,0xa0,0xac,0xb0,0xb4,0xb8,0xc4,0xc8,0xd0,0xd4,0xdc,0xe0,0xe4 }; unsigned q, ix;
        for (q = 0; q < sizeof rl / sizeof rl[0]; q++) { char m[64]; unsigned v = rd(rl[q]); snprintf(m, sizeof m, "dump reg 0x%02x = 0x%08x", rl[q], v); ulog(m); printf("reg 0x%02x = 0x%08x\n", rl[q], v); }
        for (ix = 0; ix < 10; ix++) { char m[64]; unsigned v; wr(0xa4, 0x500 | ix); v = rd(0xa8); snprintf(m, sizeof m, "dump LCD[%u] = 0x%08x", ix, v); ulog(m); printf("LCD[%u] = 0x%08x\n", ix, v); }
        fflush(stdout); }
    if (do_bios) { unsigned n = sizeof bios_seq / sizeof bios_seq[0], j;   /* faithful replay: EVERY BIOS chip access (reads too), exact offset/size, real delays */
        char m[96]; unsigned sink = 0; snprintf(m, sizeof m, "bios: %u accesses", n); ulog(m);
        for (j = 0; j < n; j++) {
            volatile unsigned char *p = (volatile unsigned char *)(rw + 0x400 + bios_seq[j].off);
            if (bios_seq[j].delay_us) usleep(bios_seq[j].delay_us);
            if (j >= skip_a && j <= skip_b) { snprintf(m, sizeof m, "bios %u: SKIPPED", j); ulog(m); continue; }
            if (j == noread) { ulog("bios: entering no-read window; 20ms PLL settle"); usleep(20000); }
            if (!bios_seq[j].wr && j >= noread) continue;
            if (nocrtc && bios_seq[j].wr && bios_seq[j].off == 0x1f) { snprintf(m, sizeof m, "bios %u: SKIP CRTC_GEN_CNTL byte3 write 0x%x", j, bios_seq[j].val); ulog(m); continue; }
            snprintf(m, sizeof m, "bios %u: %s off 0x%02x size %u val 0x%x", j, bios_seq[j].wr ? "WR" : "RD", bios_seq[j].off, bios_seq[j].size, bios_seq[j].val); ulog(m);
            if (bios_seq[j].wr) { if (bios_seq[j].size == 1) *p = (unsigned char)bios_seq[j].val; else OSWriteLittleInt16((void *)p, 0, bios_seq[j].val); }
            else { if (bios_seq[j].size == 1) sink += *p; else sink += OSReadLittleInt16((void *)p, 0); }
            __asm__ volatile("eieio" ::: "memory");
        }
        snprintf(m, sizeof m, "bios: replay complete (sink %u)", sink); ulog(m); usleep(50000); maxsteps = 0; }
    for (i = 0; i < nsteps && i < maxsteps; i++) {
        unsigned k = post_seq[i].kind, r = post_seq[i].reg, v = post_seq[i].val;
        if (have_sel) { if (!((k == 1 && i <= 35) || sel[i])) continue; }  /* sel=: PLL 0-35 plus listed steps only */
        else if (k == 3 && !full) continue;
        if (only_pll && k != 1) continue;
        { char m[96]; snprintf(m, sizeof m, "step %u kind %u reg 0x%02x val 0x%08x", i, k, r, v); ulog(m); }
        if (lg) { fprintf(lg, "step %u kind %u reg 0x%02x val 0x%08x\n", i, k, r, v); fflush(lg); fsync(fileno(lg)); }
        if (do_sdram && (i == 24 || i == 105 || i == 117)) { /* BIOS byte-lane handshake on EXT_MEM_CNTL low byte, dropped from the table */
            static const unsigned h1[3] = { 0x2, 0xa, 0xe }, h2[3] = { 0x3, 0xb, 0xf }; const unsigned *hv = (i == 24) ? h1 : h2; unsigned q;
            for (q = 0; q < 3; q++) { char m[64]; snprintf(m, sizeof m, "sdram@%u: EXT_MEM_CNTL lowbyte=0x%x", i, hv[q]); ulog(m); wr(0xac, (rd(0xac) & ~0xffu) | hv[q]); usleep(2000); }
            usleep(60000);
        }
        if (k == 1) {
            unsigned cc; ulog(" pll: read CLOCK_CNTL"); cc = rd(0x90) & ~0x00ffff00u; ulog(" pll: read ok; write addr+data+WR_EN");
            wr(0x90, cc | (r << 10) | 0x200 | (v << 16));
            ulog(" pll: write1 ok");
            usleep(1000);
            wr(0x90, cc | (r << 10) | (v << 16));
            ulog(" pll: write2 ok");
            usleep(r == 0x0c ? 60000 : 1000);
            if (r == 0x03 && pllwait) { ulog(" pll: extra wait after MCLK_FB_DIV"); usleep(pllwait * 1000); }
        } else {
            wr(r, v);
            usleep((r == 0xb0 || r == 0xac || r == 0xd0 || r == 0xa0) ? 500 : 20);
        }
    }
    usleep(20000);
    ulog("replay done");
    if (lg) { fprintf(lg, "replay done\n"); fflush(lg); fsync(fileno(lg)); }
    printf("after:  MEM_CNTL=0x%08x EXT_MEM_CNTL=0x%08x CLOCK_CNTL=0x%08x CONFIG_STAT0=0x%08x MEM_BUF_CNTL=0x%08x MEM_ADDR_CFG=0x%08x BUS_CNTL=0x%08x\n", rd(0xb0), rd(0xac), rd(0x90), rd(0xe4), rd(0x2c), rd(0x34), rd(0xa0));
    if (do_vram1) { unsigned v; ulog("vram1: write 0x600df00d @0"); vwr(0, 0x600df00d); ulog("vram1: written; reading back"); v = vrd(0); { char m[64]; snprintf(m, sizeof m, "vram1: read 0x%08x", v); ulog(m); printf("vram1 @0: wrote 0x600df00d read 0x%08x\n", v); } fflush(stdout); }
    if (do_sweep) { if (lg) { fprintf(lg, "sweep start\n"); fflush(lg); fsync(fileno(lg)); } printf("sweep after POST:\n"); fflush(stdout); sweep(); }
    IOConnectUnmapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}
