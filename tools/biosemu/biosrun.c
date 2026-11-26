/*
 * biosrun.c - run an ATI Rage (mach64) PCI option ROM's init entry under libx86emu.
 *
 *   biosrun [-s] [-m maxinstr] [-t tracefile] [-M] [-H] [-S] [-X] [-V hexmode] rage.rom
 *   -X: after INIT, probe the ROM's INT 10h VBE services (4F00/4F01);  -V mode: then set that VBE mode (4F02) through the ROM
 *
 * The chip is behind a "backend" (struct chip_ops).  This file contains the SIMULATED backend, used to validate the emulator
 * (host x86-64 build, big-endian PPC build under qemu-ppc, and the earlier Unicorn run must all produce the same trace).
 * A hardware backend (real BAR1 I/O window) plugs into the same interface; nothing here touches hardware.
 *
 * Trace format = the Unicorn harness' (bios_emu.py), so traces can be diffed:
 *   IN/OUT <ip> <port> <size> <val>, CHIP-RD/CHIP-WR ..., CFGRD/CFGWR ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HW
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#endif
#include "x86emu.h"

#define ROMSEG   0xC000u
#define IOBASE   0x0400u
#define BAR0     0x81000000u
#define BAR2     0x80085000u
#define BDF      0x0010u
#define STUB     0xF0000u

static FILE *tf;
static int opt_vline2;       /* simulated CRTC_VLINE: tick both the low and the high (CRNT) half */
static int opt_stop42, opt_hw;
static int opt_memlog;
static int opt_vbe;          /* -X: after INIT, probe the ROM's INT 10h VBE services; -V hex: then set that VBE mode */
static unsigned opt_vbemode;       /* log VGA-window / BDA / IVT memory accesses */

/* ------------------------------------------------------------------------------------------------------------------------ */
/* trace output                                                                                                              */
/* ------------------------------------------------------------------------------------------------------------------------ */
#include <stdarg.h>
#include <sys/time.h>
static void T(const char *fmt, ...)
{
    va_list ap;
    static int ts = -1; static unsigned long long t0;
    if (ts < 0) { ts = getenv("BIOS_TSTAMP") != NULL; }   /* BIOS_TSTAMP=1: prefix every trace line with "@<microseconds since the first line> " (real elapsed time, for replaying the ROM's delays) */
    if (ts) { struct timeval tv; unsigned long long us; gettimeofday(&tv, NULL); us = (unsigned long long)tv.tv_sec * 1000000ull + tv.tv_usec; if (!t0) t0 = us; fprintf(tf, "@%llu ", us - t0); }
    va_start(ap, fmt); vfprintf(tf, fmt, ap); va_end(ap); fputc('\n', tf);
}

/* ------------------------------------------------------------------------------------------------------------------------ */
/* backend interface                                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------------ */
struct chip_ops {
    unsigned (*in)(unsigned port, unsigned size);
    void     (*out)(unsigned port, unsigned size, unsigned val);
};

/* ---- simulated chip (port of bios_emu.py's model) ---- */
static uint32_t sregs[0x100];            /* dword register file indexed by reg/4 */
static uint8_t  pll[64] = { 0xcc,0xcc,0x24,0xdf,0xf6,0x04,0x00,0xfd,0x8e,0x9e,0x65,0x05,0x40,0x00,0x00,0x00 };
static uint32_t lcd[64];
static uint8_t  vidx[0x400];
static uint8_t  vga_idxdata[0x400][256];
static uint32_t vga_port[0x10000];
static uint32_t vs_toggle;

static const char *rname(unsigned r)
{
    switch (r & ~3u) {
    case 0x00: return "CRTC_H_TOTAL_DISP"; case 0x04: return "CRTC_H_SYNC"; case 0x08: return "CRTC_V_TOTAL_DISP"; case 0x0c: return "CRTC_V_SYNC";
    case 0x10: return "CRTC_VLINE"; case 0x14: return "CRTC_OFF_PITCH"; case 0x18: return "CRTC_INT_CNTL"; case 0x1c: return "CRTC_GEN_CNTL";
    case 0x20: return "DSP_CONFIG"; case 0x24: return "DSP_ON_OFF"; case 0x28: return "TIMER_CONFIG"; case 0x2c: return "MEM_BUF_CNTL";
    case 0x34: return "MEM_ADDR_CONFIG"; case 0x78: return "GP_IO"; case 0x7c: return "HW_DEBUG"; case 0x80: return "SCRATCH0"; case 0x84: return "SCRATCH1";
    case 0x90: return "CLOCK_CNTL"; case 0x94: return "CONFIG_STAT1"; case 0x98: return "CONFIG_STAT2"; case 0xa0: return "BUS_CNTL";
    case 0xa4: return "LCD_INDEX"; case 0xa8: return "LCD_DATA"; case 0xac: return "EXT_MEM_CNTL"; case 0xb0: return "MEM_CNTL"; case 0xc0: return "DAC_REGS";
    case 0xc4: return "DAC_CNTL"; case 0xc8: return "HORZ_STRETCH"; case 0xcc: return "VERT_STRETCH"; case 0xd0: return "GEN_TEST_CNTL";
    case 0xd4: return "LCD_GEN_CNTL"; case 0xd8: return "POWER_MGMT"; case 0xdc: return "CONFIG_CNTL"; case 0xe0: return "CHIP_ID"; case 0xe4: return "CONFIG_STAT0";
    }
    return NULL;
}
static void Tname(const char *kind, unsigned reg, unsigned size, unsigned val, int nv_valid, unsigned nv, unsigned lane)
{
    const char *n = rname(reg);
    char nb[16];
    if (!n) { snprintf(nb, sizeof nb, "0x%x", reg & ~3u); n = nb; }
    if (nv_valid) T("%s 0x%x %s 0x%x 0x%x 0x%x 0x%x", kind, reg & ~3u, n, size, val, nv, lane);
    else          T("%s 0x%x %s 0x%x 0x%x", kind, reg & ~3u, n, size, val);
}
/* hardware backend trace: same line as Tname() plus the EXACT byte offset the access used (" o=0x.."), which Tname() loses by aligning to the dword */
static void Tname_o(const char *kind, unsigned off, unsigned size, unsigned val)
{
    const char *n = rname(off & ~3u); char nb[16];
    if (!n) { snprintf(nb, sizeof nb, "0x%x", off & ~3u); n = nb; }
    T("%s 0x%x %s 0x%x 0x%x o=0x%x", kind, off & ~3u, n, size, val, off);
}
static uint32_t chip_rd(unsigned reg)
{
    uint32_t v = sregs[reg >> 2];
    if (reg == 0x90) v = (v & 0xff00ffffu) | ((uint32_t)pll[(v >> 10) & 0x3f] << 16);
    if (reg == 0xa8) v = lcd[sregs[0xa4 >> 2] & 0x3f];
    if (reg == 0x10) { v = (v + 1) & 0x7ff; if (opt_vline2) v |= v << 16; sregs[reg >> 2] = v; }
    return v;
}
static void chip_wr(unsigned reg, uint32_t v)
{
    sregs[reg >> 2] = v;
    if (reg == 0x90 && (v & 0x200)) pll[(v >> 10) & 0x3f] = (v >> 16) & 0xff;
    if (reg == 0xa8) lcd[sregs[0xa4 >> 2] & 0x3f] = v;
}
static void sim_init(void)
{
    memset(sregs, 0, sizeof sregs);
    sregs[0xe0>>2] = 0xdc004c49; sregs[0xe4>>2] = 0x00e00092; sregs[0xdc>>2] = 0x00002042; sregs[0x98>>2] = 0x00000100;
    sregs[0xa0>>2] = 0x80000101; sregs[0xb0>>2] = 0x00081838; sregs[0xc4>>2] = 0x07010080; sregs[0x1c>>2] = 0x04000000;
}
static unsigned sim_in(unsigned port, unsigned size)
{
    unsigned mask = size == 4 ? 0xffffffffu : (1u << (8 * size)) - 1;
    if (port >= IOBASE && port < IOBASE + 0x400) {
        unsigned r = port - IOBASE, b = r & 3, reg = r & ~3u;
        unsigned v = (chip_rd(reg) >> (8 * b)) & mask;
        Tname("CHIP-RD", reg, size, v, 0, 0, 0);
        return v;
    }
    if (port == 0x3da) { vs_toggle ^= 9; return vs_toggle; }
    if (port == 0x3c5 || port == 0x3cf || port == 0x3d5) return vga_idxdata[port - 1][vidx[port - 1]];
    if (port == 0x3c9) return 0;
    return vga_port[port] & mask;
}
static void sim_out(unsigned port, unsigned size, unsigned val)
{
    if (port >= IOBASE && port < IOBASE + 0x400) {
        unsigned r = port - IOBASE, b = r & 3, reg = r & ~3u;
        uint32_t old = sregs[reg >> 2];
        uint32_t m = (size == 4 ? 0xffffffffu : ((1u << (8 * size)) - 1)) << (8 * b);
        uint32_t nv = (old & ~m) | ((val << (8 * b)) & m);
        chip_wr(reg, nv);
        Tname("CHIP-WR", reg, size, val, 1, nv, b);
        return;
    }
    if (port == 0x3c4 || port == 0x3ce || port == 0x3d4) vidx[port] = val & 0xff;
    if (port == 0x3c5 || port == 0x3cf || port == 0x3d5) vga_idxdata[port - 1][vidx[port - 1]] = val & 0xff;
    vga_port[port] = val;
}
static const struct chip_ops sim_ops = { sim_in, sim_out };

/* ---- UDP mirror: every real chip access is sent BEFORE it executes, so the last line seen is the one that hung ---- */
static int usock = -1; static struct sockaddr_in uadr; static unsigned long nacc, stop_before = 0xffffffffu;
static void ulog(const char *fmt, ...)
{
    char b[200]; va_list ap; if (usock < 0) return;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    sendto(usock, b, strlen(b), 0, (struct sockaddr *)&uadr, sizeof uadr);
}

/* ---- PIT model (ports 0x40/0x43): the BIOS' delay routine latches the counter (OUT 43h,0) and reads lo,hi; counter counts DOWN ---- */
static int pit_mode;                   /* 0 = legacy constant (matches the Unicorn baseline), 1 = virtual time, 2 = real time */
static unsigned long vticks;           /* virtual 1.193182 MHz tick count */
static unsigned pit_latch, pit_state;
static unsigned long long ticks_now(void)
{
    if (pit_mode == 2) { struct timeval tv; gettimeofday(&tv, NULL); return ((unsigned long long)tv.tv_sec * 1000000ull + tv.tv_usec) * 1193182ull / 1000000ull; }
    return vticks;
}
static int pit_in(unsigned port, unsigned *v)
{
    if (!pit_mode || port != 0x40) return 0;
    *v = pit_state == 0 ? (pit_latch & 0xff) : ((pit_latch >> 8) & 0xff); pit_state ^= 1; return 1;
}
static int pit_out(unsigned port, unsigned val)
{
    if (!pit_mode) return 0;
    if (port == 0x43) { if (pit_mode == 1) vticks += 37; pit_latch = (unsigned)(0x10000 - (ticks_now() & 0xffff)) & 0xffff; pit_state = 0; return 1; }
    return port == 0x40;   /* reload value writes: ignored, we keep a free-running 16-bit down counter */
}

#ifdef HW
/* ---- hardware backend: BAR1 I/O window (kext memory type 7), dense, register R at +R.  Needs Mach64Accelerator loaded. ---- */
static volatile unsigned char *hwio;
static unsigned hw_rd(unsigned off, unsigned size)
{
    unsigned v = 0, i;
    if (size == 4 && !(off & 3)) v = OSReadLittleInt32((void *)hwio, off);
    else if (size == 2 && !(off & 1)) v = OSReadLittleInt16((void *)hwio, off);
    else for (i = 0; i < size; i++) v |= (unsigned)hwio[off + i] << (8 * i);
    __asm__ volatile("eieio" ::: "memory");
    return v;
}
static void hw_wr(unsigned off, unsigned size, unsigned val)
{
    unsigned i;
    if (size == 4 && !(off & 3)) OSWriteLittleInt32((void *)hwio, off, val);
    else if (size == 2 && !(off & 1)) OSWriteLittleInt16((void *)hwio, off, val);
    else for (i = 0; i < size; i++) hwio[off + i] = (val >> (8 * i)) & 0xff;
    __asm__ volatile("eieio; sync" ::: "memory");
}
static unsigned hw_in(unsigned port, unsigned size)
{
    unsigned v;
    if (port < IOBASE || port >= IOBASE + 0x100) return sim_in(port, size);
    nacc++; if (nacc >= stop_before) { ulog("STOP before access %lu (RD reg 0x%x)", nacc, port - IOBASE); fprintf(stderr, "stop_before reached\n"); exit(3); }
    ulog("A%lu RD  0x%02x s%u", nacc, port - IOBASE, size);
    v = hw_rd(port - IOBASE, size);
    ulog("A%lu RD  0x%02x s%u = 0x%x", nacc, port - IOBASE, size, v);
    Tname_o("CHIP-RD", port - IOBASE, size, v);
    return v;
}
static void hw_out(unsigned port, unsigned size, unsigned val)
{
    if (port < IOBASE || port >= IOBASE + 0x100) { sim_out(port, size, val); return; }
    nacc++; if (nacc >= stop_before) { ulog("STOP before access %lu (WR reg 0x%x)", nacc, port - IOBASE); fprintf(stderr, "stop_before reached\n"); exit(3); }
    ulog("A%lu WR  0x%02x s%u = 0x%x", nacc, port - IOBASE, size, val);
    hw_wr(port - IOBASE, size, val);
    ulog("A%lu WR  done", nacc);
    Tname_o("CHIP-WR", port - IOBASE, size, val);
}
static const struct chip_ops hw_ops = { hw_in, hw_out };
static int hw_open(void)
{
    io_service_t svc; io_connect_t conn; vm_address_t a = 0; vm_size_t sz = 0;
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), 1 /* MACH64_GL_CONNECT_TYPE */, &conn)) { fprintf(stderr, "open Mach64Accelerator failed\n"); return -1; }
    { vm_address_t sa = 0; vm_size_t ss = 0;   /* same first step as the proven mach64_post path */
      if (IOConnectMapMemory(conn, 1 /* SURFACE */, mach_task_self(), &sa, &ss, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map surface failed\n"); return -1; } }
    if (IOConnectMapMemory(conn, 7, mach_task_self(), &a, &sz, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map I/O window failed\n"); return -1; }
    hwio = (volatile unsigned char *)a;
    fprintf(stderr, "I/O window at %p size 0x%x\n", (void *)a, (unsigned)sz);
    return 0;
}
#endif
static const struct chip_ops *chip = &sim_ops;

/* ------------------------------------------------------------------------------------------------------------------------ */
/* PCI config space of the card                                                                                              */
/* ------------------------------------------------------------------------------------------------------------------------ */
static uint8_t cfg[256];
static void put16(unsigned o, unsigned v) { cfg[o] = v & 0xff; cfg[o + 1] = (v >> 8) & 0xff; }
static void put32(unsigned o, uint32_t v) { put16(o, v & 0xffff); put16(o + 2, v >> 16); }
static void cfg_init(void)
{
    memset(cfg, 0, sizeof cfg);
    put16(0, 0x1002); put16(2, 0x4c49); put16(4, 0x0003); put16(6, 0x0290); cfg[8] = 0xdc; cfg[9] = 0; cfg[10] = 0; cfg[11] = 3;
    put32(0x10, BAR0); put32(0x14, IOBASE | 1); put32(0x18, BAR2); put16(0x2c, 0x1002); put16(0x2e, 0x4c49);
    cfg[0x3c] = 11; cfg[0x3d] = 1;
}

/* ------------------------------------------------------------------------------------------------------------------------ */
/* emulator glue                                                                                                             */
/* ------------------------------------------------------------------------------------------------------------------------ */
static x86emu_memio_handler_t def_memio;
static unsigned long io_count;

static unsigned cur_ip(x86emu_t *emu) { return ((unsigned)emu->x86.saved_cs << 4) + (emu->x86.saved_eip & 0xffff); }

static unsigned my_memio(x86emu_t *emu, u32 addr, u32 *val, unsigned type)
{
    unsigned t = type & ~0xffu, bits = type & 0xff, size = bits == X86EMU_MEMIO_8 ? 1 : bits == X86EMU_MEMIO_16 ? 2 : 4;
    if (t == X86EMU_MEMIO_I) {
        unsigned v; io_count++;
        if (!pit_in(addr & 0xffff, &v)) v = chip->in(addr & 0xffff, size);
        T("IN 0x%x 0x%x 0x%x 0x%x", cur_ip(emu), addr & 0xffff, size, v);
        *val = v; return 0;
    }
    if (t == X86EMU_MEMIO_O) {
        io_count++;
        if (!pit_out(addr & 0xffff, *val)) chip->out(addr & 0xffff, size, *val);
        T("OUT 0x%x 0x%x 0x%x 0x%x", cur_ip(emu), addr & 0xffff, size, *val);
        return 0;
    }
    if (opt_memlog && (t == X86EMU_MEMIO_R || t == X86EMU_MEMIO_W) && bits != X86EMU_MEMIO_8_NOPERM) {
        if ((addr >= 0xA0000 && addr < 0xC0000) || (addr >= 0x400 && addr < 0x500) || addr < 0x400) {
            unsigned r = def_memio(emu, addr, val, type);
            T("MEM%c 0x%x 0x%x 0x%x 0x%x", t == X86EMU_MEMIO_R ? 'R' : 'W', cur_ip(emu), addr, size, *val);
            return r;
        }
    }
    return def_memio(emu, addr, val, type);
}

static void setcf(x86emu_t *emu, int c) { if (c) emu->x86.R_FLG |= F_CF; else emu->x86.R_FLG &= ~F_CF; }

static int my_intr(x86emu_t *emu, u8 nr, unsigned type)
{
    unsigned ax = emu->x86.R_AX;
    unsigned ivt_ip = x86emu_read_word(emu, nr * 4), ivt_cs = x86emu_read_word(emu, nr * 4 + 2);
    (void)type;
    if (nr == 0x1a && (ax >> 8) == 0xb1) {
        unsigned fn = ax & 0xff;
        if (fn == 0x01) { emu->x86.R_AX = 1; emu->x86.R_EDX = 0x20494350; emu->x86.R_BX = 0x0210; emu->x86.R_CX = 0; setcf(emu, 0); }
        else if (fn == 0x02) {
            if (emu->x86.R_DX == 0x1002 && emu->x86.R_CX == 0x4c49 && emu->x86.R_SI == 0) { emu->x86.R_BX = BDF; emu->x86.R_AX = 0; setcf(emu, 0); }
            else { emu->x86.R_AX = 0x8600; setcf(emu, 1); }
        }
        else if (fn == 0x08 || fn == 0x09 || fn == 0x0a) {
            unsigned r = emu->x86.R_DI, sz = fn == 8 ? 1 : fn == 9 ? 2 : 4, i; uint32_t v = 0;
            for (i = 0; i < sz; i++) v |= (uint32_t)cfg[(r + i) & 0xff] << (8 * i);
            T("CFGRD 0x%x 0x%x 0x%x", r, sz, v);
            emu->x86.R_ECX = v; emu->x86.R_AX = 0; setcf(emu, 0);
        }
        else if (fn == 0x0b || fn == 0x0c || fn == 0x0d) {
            unsigned r = emu->x86.R_DI, sz = fn == 0xb ? 1 : fn == 0xc ? 2 : 4, i; uint32_t v = emu->x86.R_ECX;
            T("CFGWR 0x%x 0x%x 0x%x", r, sz, v);
            for (i = 0; i < sz; i++) cfg[(r + i) & 0xff] = (v >> (8 * i)) & 0xff;
            emu->x86.R_AX = 0; setcf(emu, 0);
        }
        else { T("PCIBIOS-unhandled 0x%x", ax); setcf(emu, 1); }
        return 1;
    }
    if (ivt_cs != 0 && (ivt_cs << 4) + ivt_ip != STUB && ivt_cs != 0xf000) {   /* a vector installed by the ROM (or anything real): dispatch normally */
        T("INT-dispatch 0x%x 0x%x", nr, ax);
        return 0;
    }
    if (nr == 0x42) {   /* system BIOS' default video entry (F000:F065): the ROM calls it for 'set mode'; the system BIOS is not modelled */
        T("INT42 0x%x bx=0x%x dx=0x%x (vector %04x:%04x) -> treated as system-BIOS no-op", ax, emu->x86.R_BX, emu->x86.R_DX, ivt_cs, ivt_ip);
        ulog("INT42 reached after %llu instr, %lu chip accesses: chip init finished", (unsigned long long)emu->x86.R_TSC, nacc);
        if (opt_stop42) x86emu_stop(emu);
        return 1;
    }
    T("INT-unhandled 0x%x 0x%x (vector %04x:%04x)", nr, ax, ivt_cs, ivt_ip);
    if (nr == 0x15) { setcf(emu, 1); emu->x86.R_AX = 0x8600; }
    return 1;
}


/* run `int nr` through the vector the ROM installed (IVT entry), returning to the HLT stub; registers are taken from / left in emu->x86 */
static unsigned rd16(x86emu_t *emu, unsigned a) { return x86emu_read_byte_noperm(emu, a) | (x86emu_read_byte_noperm(emu, a + 1) << 8); }
static int run_int(x86emu_t *emu, unsigned nr, unsigned long maxins)
{
    unsigned ip = rd16(emu, nr * 4), cs = rd16(emu, nr * 4 + 2), sp = 0x6e00;
    if (cs == 0 && ip == 0) return -1;
    emu->x86.mode &= ~_MODE_HALTED;
    x86emu_set_seg_register(emu, emu->x86.seg + R_SS_INDEX, 0);
    sp -= 2; x86emu_write_word(emu, sp, 0x0202);
    sp -= 2; x86emu_write_word(emu, sp, 0xF000);
    sp -= 2; x86emu_write_word(emu, sp, 0x0000);
    emu->x86.R_ESP = sp;
    x86emu_set_seg_register(emu, emu->x86.seg + R_CS_INDEX, cs);
    emu->x86.R_EIP = ip;
    emu->max_instr = emu->x86.R_TSC + maxins;
    x86emu_run(emu, X86EMU_RUN_MAX_INSTR);
    return (emu->x86.mode & _MODE_HALTED) ? 0 : -2;
}
static FILE *itrace;
static void iflush(x86emu_t *emu, char *buf, unsigned size) { if (itrace) fwrite(buf, 1, size, itrace); }
static void crtc_dump(const char *tag)
{
#ifdef HW
    static const struct { unsigned r; const char *n; } R[] = { {0x00,"CRTC_H_TOTAL_DISP"}, {0x04,"CRTC_H_SYNC_STRT_WID"}, {0x08,"CRTC_V_TOTAL_DISP"}, {0x0c,"CRTC_V_SYNC_STRT_WID"},
        {0x14,"CRTC_OFF_PITCH"}, {0x1c,"CRTC_GEN_CNTL"}, {0x90,"CLOCK_CNTL"}, {0xc4,"DAC_CNTL"}, {0xa0,"BUS_CNTL"}, {0xb0,"MEM_CNTL"}, {0xe0,"CONFIG_CNTL?"}, {0xe4,"CONFIG_STAT0"} };
    unsigned i;
    if (!opt_hw) goto simdump;
    fprintf(stderr, "-- %s\n", tag);
    for (i = 0; i < sizeof R / sizeof R[0]; i++) fprintf(stderr, "   %-22s [0x%02x] = 0x%08x\n", R[i].n, R[i].r, hw_rd(R[i].r, 4));
    return;
simdump:
#endif
    { static const struct { unsigned r; const char *n; } S[] = { {0x00,"CRTC_H_TOTAL_DISP"}, {0x04,"CRTC_H_SYNC_STRT_WID"}, {0x08,"CRTC_V_TOTAL_DISP"}, {0x0c,"CRTC_V_SYNC_STRT_WID"}, {0x10,"CRTC_VLINE"}, {0x14,"CRTC_OFF_PITCH"}, {0x1c,"CRTC_GEN_CNTL"}, {0x20,"DSP_CONFIG"}, {0x24,"DSP_ON_OFF"}, {0x90,"CLOCK_CNTL"}, {0xc4,"DAC_CNTL"}, {0xa0,"BUS_CNTL"} }; unsigned k; fprintf(stderr, "-- SIM %s\n", tag); for (k = 0; k < sizeof S / sizeof S[0]; k++) fprintf(stderr, "   %-22s [0x%02x] = 0x%08x\n", S[k].n, S[k].r, sregs[S[k].r >> 2]); fprintf(stderr, "   LCD_GEN_CTRL(idx1)=0x%08x  idx0=0x%08x idx2=0x%08x idx3=0x%08x idx4=0x%08x idx5=0x%08x\n", lcd[1], lcd[0], lcd[2], lcd[3], lcd[4], lcd[5]); }
}
static void vbe_probe(x86emu_t *emu)
{
    unsigned i, buf = 0x8000; unsigned ivt_ip = rd16(emu, 0x40), ivt_cs = rd16(emu, 0x42); int rc;
    fprintf(stderr, "IVT[10h] = %04x:%04x\n", ivt_cs, ivt_ip);
    for (i = 0; i < 512; i++) x86emu_write_byte_noperm(emu, buf + i, 0);
    x86emu_write_byte_noperm(emu, buf, 'V'); x86emu_write_byte_noperm(emu, buf + 1, 'B'); x86emu_write_byte_noperm(emu, buf + 2, 'E'); x86emu_write_byte_noperm(emu, buf + 3, '2');
    emu->x86.R_AX = 0x4f00; x86emu_set_seg_register(emu, emu->x86.seg + R_ES_INDEX, 0); emu->x86.R_DI = buf;
    rc = run_int(emu, 0x10, 5000000);
    fprintf(stderr, "VBE 4F00: rc=%d ax=%04x sig=%c%c%c%c ver=%04x oem=%04x:%04x\n", rc, emu->x86.R_AX, x86emu_read_byte_noperm(emu, buf), x86emu_read_byte_noperm(emu, buf + 1), x86emu_read_byte_noperm(emu, buf + 2), x86emu_read_byte_noperm(emu, buf + 3), rd16(emu, buf + 4), rd16(emu, buf + 8), rd16(emu, buf + 10));
    if (rc == 0 && emu->x86.R_AX == 0x4f) {
        unsigned mp = (rd16(emu, buf + 16) << 4) + rd16(emu, buf + 14), m, n = 0; fprintf(stderr, "  mode list at %05x:", mp);
        for (; n < 64; n++) { m = rd16(emu, mp + 2 * n); if (m == 0xffff) break; fprintf(stderr, " %04x", m); }
        fprintf(stderr, "\n");
        for (i = 0; i < n; i++) {
            unsigned mm = rd16(emu, mp + 2 * i), mb = 0x8200, k;
            for (k = 0; k < 256; k++) x86emu_write_byte_noperm(emu, mb + k, 0);
            emu->x86.R_AX = 0x4f01; emu->x86.R_CX = mm; emu->x86.R_DI = mb; x86emu_set_seg_register(emu, emu->x86.seg + R_ES_INDEX, 0);
            rc = run_int(emu, 0x10, 2000000);
            if (rc == 0 && emu->x86.R_AX == 0x4f)
                fprintf(stderr, "  mode %04x: attr %04x  %ux%u  bpp %u  pitch %u  LFB %08x  memmodel %u\n", mm, rd16(emu, mb), rd16(emu, mb + 18), rd16(emu, mb + 20), x86emu_read_byte_noperm(emu, mb + 25), rd16(emu, mb + 16), rd16(emu, mb + 40) | (rd16(emu, mb + 42) << 16), x86emu_read_byte_noperm(emu, mb + 27));
        }
    }
    crtc_dump("CRTC state after INIT, before any mode set");
    if (getenv("BIOS_POKE")) {   /* sim only: reg:val hex, sets the simulated chip register */
        unsigned pr = strtoul(getenv("BIOS_POKE"), NULL, 16); char *c = strchr(getenv("BIOS_POKE"), ':');
        if (c) {
            unsigned pv = strtoul(c + 1, NULL, 16);
#ifdef HW
            if (opt_hw) { unsigned before = hw_rd(pr, 4); hw_wr(pr, 4, pv); fprintf(stderr, "poked HW reg 0x%02x: 0x%08x -> 0x%08x (readback 0x%08x)\n", pr, before, pv, hw_rd(pr, 4)); }
            else
#endif
            { sregs[pr >> 2] = pv; fprintf(stderr, "poked sim reg 0x%02x = 0x%08x\n", pr, pv); }
        }
    }
    if (getenv("BIOS_PREMODE")) {
        emu->x86.R_AX = strtoul(getenv("BIOS_PREMODE"), NULL, 16); if (getenv("BIOS_PRECX")) emu->x86.R_CX = strtoul(getenv("BIOS_PRECX"), NULL, 16); rc = run_int(emu, 0x10, 40000000);
        fprintf(stderr, "INT10 pre-mode %s: rc=%d ax=%04x\n", getenv("BIOS_PREMODE"), rc, emu->x86.R_AX);
        crtc_dump("CRTC state after the standard mode set");
    }
    { unsigned k; fprintf(stderr, "scratch regs after premode:"); for (k = 0x80; k <= 0x8c; k += 4) fprintf(stderr, " [%02x]=%08x", k, sregs[k >> 2]); fprintf(stderr, "\n"); }
    if (getenv("BIOS_POKE2")) {   /* sim only: reg:val, applied after the pre-mode call, before the mode set */
        unsigned pr = strtoul(getenv("BIOS_POKE2"), NULL, 16); char *c = strchr(getenv("BIOS_POKE2"), ':');
        if (c) { sregs[pr >> 2] = strtoul(c + 1, NULL, 16); fprintf(stderr, "poked2 sim reg 0x%02x = 0x%08x\n", pr, sregs[pr >> 2]); }
    }
    if (getenv("BIOS_EDID")) {   /* VBE/DDC 4F15: BL=0 capabilities, BL=1 read EDID block DX into ES:DI */
        unsigned eb = 0x8400, k;
        emu->x86.R_AX = getenv("BIOS_A013") ? 0xa013 : 0x4f15; emu->x86.R_BX = 0; emu->x86.R_CX = 0; emu->x86.R_DX = 0; x86emu_set_seg_register(emu, emu->x86.seg + R_ES_INDEX, 0); emu->x86.R_DI = eb;
        rc = run_int(emu, 0x10, 40000000);
        fprintf(stderr, "VBE 4F15 BL=0: rc=%d ax=%04x bx=%04x\n", rc, emu->x86.R_AX, emu->x86.R_BX);
        for (k = 0; k < 128; k++) x86emu_write_byte_noperm(emu, eb + k, 0);
        emu->x86.R_AX = getenv("BIOS_A013") ? 0xa013 : 0x4f15; emu->x86.R_BX = 1; emu->x86.R_CX = 128; emu->x86.R_DX = 0; x86emu_set_seg_register(emu, emu->x86.seg + R_ES_INDEX, 0); emu->x86.R_DI = eb;
        rc = run_int(emu, 0x10, 40000000);
        fprintf(stderr, "VBE 4F15 BL=1: rc=%d ax=%04x\n", rc, emu->x86.R_AX);
        for (k = 0; k < 128; k += 16) { unsigned j; fprintf(stderr, "  EDID %02x:", k); for (j = 0; j < 16; j++) fprintf(stderr, " %02x", x86emu_read_byte_noperm(emu, eb + k + j)); fprintf(stderr, "\n"); }
    }
    if (opt_vbemode) {
        emu->x86.R_AX = 0x4f02; emu->x86.R_BX = opt_vbemode;
        if (getenv("BIOS_ITRACE")) { itrace = fopen(getenv("BIOS_ITRACE"), "w"); x86emu_set_log(emu, 1 << 20, iflush); emu->log.trace = X86EMU_TRACE_CODE | X86EMU_TRACE_REGS; }
        rc = run_int(emu, 0x10, 40000000);
        if (itrace) { x86emu_clear_log(emu, 1); fclose(itrace); itrace = 0; }
        fprintf(stderr, "VBE 4F02 mode %04x: rc=%d ax=%04x\n", opt_vbemode, rc, emu->x86.R_AX);
        crtc_dump("CRTC state after the BIOS mode set");
    }
}

int main(int argc, char **argv)
{
    const char *romfile = NULL, *tracefile = "trace_c.txt";
    unsigned long maxins = 40000000;
    int i; FILE *f; unsigned char rom[0x10000]; size_t n;
    x86emu_t *emu; unsigned sp;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-m") && i + 1 < argc) maxins = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) tracefile = argv[++i];
        else if (!strcmp(argv[i], "-M")) opt_memlog = 1;
        else if (!strcmp(argv[i], "-v")) opt_vline2 = 1;
        else if (!strcmp(argv[i], "-S")) opt_stop42 = 1;
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) pit_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) stop_before = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-u") && i + 1 < argc) {
            usock = socket(AF_INET, SOCK_DGRAM, 0); memset(&uadr, 0, sizeof uadr); uadr.sin_family = AF_INET; uadr.sin_port = htons(9999);
            uadr.sin_addr.s_addr = inet_addr(argv[++i]);
        }
        else if (!strcmp(argv[i], "-H")) opt_hw = 1;
        else if (!strcmp(argv[i], "-X")) opt_vbe = 1;
        else if (!strcmp(argv[i], "-V") && i + 1 < argc) { opt_vbe = 1; opt_vbemode = strtoul(argv[++i], NULL, 16); }
        else romfile = argv[i];
    }
    if (!romfile) { fprintf(stderr, "usage: biosrun [-m maxins] [-t trace] [-M] rage.rom\n"); return 2; }
    if (!(f = fopen(romfile, "rb"))) { perror(romfile); return 1; }
    memset(rom, 0, sizeof rom); n = fread(rom, 1, sizeof rom, f); fclose(f);
    if (!(tf = fopen(tracefile, "w"))) { perror(tracefile); return 1; }

    sim_init(); cfg_init();
    if (opt_hw) {
#ifdef HW
        if (hw_open()) return 1;
        chip = &hw_ops; if (!pit_mode) pit_mode = 2;
        ulog("biosrun: hardware backend, pit real-time");
#else
        fprintf(stderr, "built without -DHW\n"); return 2;
#endif
    }

    emu = x86emu_new(X86EMU_PERM_RW | X86EMU_PERM_X, X86EMU_PERM_RW);
    def_memio = x86emu_set_memio_handler(emu, my_memio);
    x86emu_set_intr_handler(emu, my_intr);

    /* 1 MB of RAM, ROM at C000:0, HLT stub at F000:0 */
    x86emu_set_perm(emu, 0, 0xfffff, X86EMU_PERM_RWX);
    for (i = 0; i < 0x10000; i++) x86emu_write_byte_noperm(emu, ROMSEG * 16 + i, rom[i]);
    x86emu_write_byte_noperm(emu, STUB, 0xf4);

    x86emu_set_seg_register(emu, emu->x86.seg + R_SS_INDEX, 0);
    x86emu_set_seg_register(emu, emu->x86.seg + R_DS_INDEX, 0x40);
    x86emu_set_seg_register(emu, emu->x86.seg + R_ES_INDEX, 0);
    x86emu_set_seg_register(emu, emu->x86.seg + R_CS_INDEX, ROMSEG);
    emu->x86.R_EIP = 3;
    emu->x86.R_AX = BDF;
    sp = 0x7000;
    sp -= 2; x86emu_write_word(emu, sp, 0xF000);       /* far return to the HLT stub */
    sp -= 2; x86emu_write_word(emu, sp, 0x0000);
    emu->x86.R_ESP = sp;

    emu->max_instr = maxins;
    x86emu_run(emu, X86EMU_RUN_MAX_INSTR);
    fprintf(stderr, "stopped at %04x:%04x ax=%04x  instr=%llu io=%lu halted=%d\n", emu->x86.R_CS, emu->x86.R_IP, emu->x86.R_AX,
            (unsigned long long)emu->x86.R_TSC, io_count, (emu->x86.mode & _MODE_HALTED) ? 1 : 0);
    ulog("biosrun: finished, %lu chip accesses, %lu instr", nacc, (unsigned long)emu->x86.R_TSC);
    if (opt_vbe) vbe_probe(emu);
#ifdef HW
    if (opt_hw) {
        /* Mac platform fix-up (NOT part of the PC option ROM): Apple's ATIRagePro assumes Open Firmware left BUS_CNTL.BUS_EXT_REG_EN
         * (bit 27) set.  The PC BIOS leaves it clear, and with it clear the 3D setup/vertex registers latch on readback but never
         * trigger the setup engine when written by CPU (PIO) -- so Apple's submit_buffer path draws nothing.  Set it here so the card
         * is in the state Apple's driver expects.  (Found Nov 20: PIO recipe 0 -> 4080 px with only this bit changed.) */
        unsigned bc = hw_rd(0xa0, 4);
        hw_wr(0xa0, 4, bc | (1u << 27));
        ulog("biosrun: platform fix-up BUS_CNTL 0x%08x -> 0x%08x (BUS_EXT_REG_EN)", bc, hw_rd(0xa0, 4));
    }
#endif
    fclose(tf);
    (void)n;
    return 0;
}
