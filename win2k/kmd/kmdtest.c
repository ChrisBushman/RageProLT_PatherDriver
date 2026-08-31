/*
 * kmdtest.c - user-mode control + register test for mach64kmd.sys (Win2000).
 *
 * Installs/starts/stops/removes the kernel helper via the SCM, and drives its
 * register IOCTLs.  CRT-free MinGW build (own entry, only kernel32/advapi32/user32)
 * so it loads on Win2000 (modern MinGW CRT imports EncodePointer, which is XP SP2+).
 *
 * Usage:
 *   kmdtest install <C:\path\mach64kmd.sys>   create the kernel service
 *   kmdtest start | stop | remove
 *   kmdtest peek                              MAP 0x81000000/0x1000 + read config regs
 *   kmdtest rd <off>                          read one reg (hex offset)
 *   kmdtest wr <off> <val>                    write one reg
 *   kmdtest auto <C:\path\mach64kmd.sys>      install + start + peek (one shot)
 *
 * Build: see win2k/kmd/Makefile.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <winioctl.h>

__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);

#define SVC_NAME "Mach64Kmd"
#define DEV_PATH "\\\\.\\Mach64Kmd"

#define MACH64_TYPE 0x8000
#define CTL(f) CTL_CODE(MACH64_TYPE, (f), METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_MAP   CTL(0x800)
#define IOCTL_MACH64_UNMAP CTL(0x801)
#define IOCTL_MACH64_RD32  CTL(0x802)
#define IOCTL_MACH64_WR32  CTL(0x803)
#define IOCTL_MACH64_RDBLK CTL(0x804)
#define IOCTL_MACH64_WRBLK CTL(0x805)
#define IOCTL_MACH64_WRREGS CTL(0x806)

/* mach64 register offsets (docs/dri-reference/drm_mach64_drv.h) */
#define CONFIG_CHIP_ID 0x04e0
#define CONFIG_CNTL    0x04dc
#define CONFIG_STAT0   0x04e4
#define MEM_CNTL       0x04b0
#define EXT_MEM_CNTL   0x04ac
#define SCRATCH_REG0   0x0480

/* 2D solid-fill register set (common/mach64_regs.h; sequence per gld_render.c) */
#define R_Z_CNTL           0x054c
#define R_SCALE_3D_CNTL    0x05fc
#define R_GUI_TRAJ_CNTL    0x0730
#define R_DP_PIX_WIDTH     0x06d0
#define R_DP_FRGD_CLR      0x06c4
#define R_DP_WRITE_MASK    0x06c8
#define R_DP_MIX           0x06d4
#define R_DP_SRC           0x06d8
#define R_SC_LEFT_RIGHT    0x06a8
#define R_SC_TOP_BOTTOM    0x06b4
#define R_CLR_CMP_CNTL     0x0708
#define R_DST_OFF_PITCH    0x0500
#define R_DST_X_Y          0x06e8
#define R_DST_WIDTH_HEIGHT 0x06ec
#define R_GUI_STAT         0x0738
#define GUI_ACTIVE         0x00000001u

/* Combined mapping: FB at 0x80000000 (16MB) then regs at 0x81000000, contiguous.
 * Map both as one window; register R lives at REGOFF(R), VRAM byte B at B. */
#define COMBINED_PHYS 0x80000000ULL
#define COMBINED_SIZE 0x01001000UL     /* 16MB + 4KB */
#define REG_BIAS      0x01000000UL     /* regs start 16MB into the mapping */
#define REGOFF(R)     (REG_BIAS + (R))
#define FBTEST_OFF    0x00400000UL     /* 4MB into VRAM: off-screen scratch */

static HANDLE g_out;
static char   g_buf[512];
static void outs(const char *s) { DWORD n; WriteFile(g_out, s, lstrlenA(s), &n, 0); }
#define OUTF(...) do { wsprintfA(g_buf, __VA_ARGS__); outs(g_buf); } while (0)

static ULONG parse_num(const char *s)
{
    ULONG v = 0; int hex = 0;
    if (s && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { hex = 1; s += 2; }
    for (; s && *s; s++) {
        char c = *s; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (hex) v = (v << 4) | d; else v = v * 10 + d;
    }
    return v;
}

static int split_args(char *cl, char **argv, int argmax)
{
    int n = 0;
    while (*cl && n < argmax) {
        while (*cl == ' ' || *cl == '\t') cl++;
        if (!*cl) break;
        if (*cl == '"') { cl++; argv[n++] = cl; while (*cl && *cl != '"') cl++; }
        else            { argv[n++] = cl;        while (*cl && *cl != ' ' && *cl != '\t') cl++; }
        if (*cl) *cl++ = 0;
    }
    return n;
}

static int streq(const char *a, const char *b) { return lstrcmpA(a, b) == 0; }

/* ---- SCM operations ---- */
static int svc_install(const char *syspath)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { OUTF("OpenSCManager failed %lu\n", GetLastError()); return 1; }
    SC_HANDLE svc = CreateServiceA(scm, SVC_NAME, SVC_NAME, SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        syspath, NULL, NULL, NULL, NULL, NULL);
    if (!svc) {
        DWORD e = GetLastError();
        if (e == ERROR_SERVICE_EXISTS) { outs("service already exists (ok)\n"); CloseServiceHandle(scm); return 0; }
        OUTF("CreateService failed %lu\n", e); CloseServiceHandle(scm); return 1;
    }
    outs("service created\n");
    CloseServiceHandle(svc); CloseServiceHandle(scm); return 0;
}

static int svc_ctl(int start, int remove)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { OUTF("OpenSCManager failed %lu\n", GetLastError()); return 1; }
    SC_HANDLE svc = OpenServiceA(scm, SVC_NAME, SERVICE_ALL_ACCESS);
    if (!svc) { OUTF("OpenService failed %lu\n", GetLastError()); CloseServiceHandle(scm); return 1; }
    int rc = 0;
    if (remove) {
        if (!DeleteService(svc)) { OUTF("DeleteService failed %lu\n", GetLastError()); rc = 1; }
        else outs("service removed\n");
    } else if (start) {
        if (!StartServiceA(svc, 0, NULL)) {
            DWORD e = GetLastError();
            if (e == ERROR_SERVICE_ALREADY_RUNNING) outs("already running (ok)\n");
            else { OUTF("StartService failed %lu\n", e); rc = 1; }
        } else outs("driver started\n");
    } else {
        SERVICE_STATUS ss;
        if (!ControlService(svc, SERVICE_CONTROL_STOP, &ss)) { OUTF("stop failed %lu\n", GetLastError()); rc = 1; }
        else outs("driver stopped\n");
    }
    CloseServiceHandle(svc); CloseServiceHandle(scm); return rc;
}

/* ---- device IOCTLs ---- */
static HANDLE dev_open(void)
{
    HANDLE h = CreateFileA(DEV_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) OUTF("open %s failed %lu\n", DEV_PATH, GetLastError());
    return h;
}

static int dev_map(HANDLE h, ULONGLONG phys, ULONG size)
{
    unsigned char in[16]; DWORD br;
    *(ULONGLONG *)(in + 0) = phys;
    *(ULONG *)(in + 8) = size;
    if (!DeviceIoControl(h, IOCTL_MACH64_MAP, in, 12, NULL, 0, &br, NULL)) {
        OUTF("MAP(0x%08lX,0x%lX) failed %lu\n", (ULONG)phys, size, GetLastError());
        return 1;
    }
    OUTF("mapped phys 0x%08lX size 0x%lX\n", (ULONG)phys, size);
    return 0;
}

static int dev_rd(HANDLE h, ULONG off, ULONG *out)
{
    DWORD br; ULONG v = 0;
    if (!DeviceIoControl(h, IOCTL_MACH64_RD32, &off, 4, &v, 4, &br, NULL)) {
        OUTF("RD32(0x%04lX) failed %lu\n", off, GetLastError()); return 1;
    }
    *out = v; return 0;
}

static int dev_wr(HANDLE h, ULONG off, ULONG val)
{
    unsigned char in[8]; DWORD br;
    *(ULONG *)(in + 0) = off; *(ULONG *)(in + 4) = val;
    if (!DeviceIoControl(h, IOCTL_MACH64_WR32, in, 8, NULL, 0, &br, NULL)) {
        OUTF("WR32(0x%04lX,0x%08lX) failed %lu\n", off, val, GetLastError()); return 1;
    }
    return 0;
}

static int dev_rdblk(HANDLE h, ULONG off, void *out, ULONG len)
{
    unsigned char in[8]; DWORD br;
    *(ULONG *)(in + 0) = off; *(ULONG *)(in + 4) = len;
    if (!DeviceIoControl(h, IOCTL_MACH64_RDBLK, in, 8, out, len, &br, NULL)) {
        OUTF("RDBLK(0x%lx,%lu) failed %lu\n", off, len, GetLastError()); return 1;
    }
    return 0;
}

static int dev_wrblk(HANDLE h, ULONG off, const void *data, ULONG len)
{
    static unsigned char in[8 + 0x10000]; DWORD br; ULONG i;
    if (len > 0x10000) return 1;
    *(ULONG *)(in + 0) = off; *(ULONG *)(in + 4) = len;
    for (i = 0; i < len; i++) in[8 + i] = ((const unsigned char *)data)[i];
    if (!DeviceIoControl(h, IOCTL_MACH64_WRBLK, in, 8 + len, NULL, 0, &br, NULL)) {
        OUTF("WRBLK(0x%lx,%lu) failed %lu\n", off, len, GetLastError()); return 1;
    }
    return 0;
}

static int dev_wrregs(HANDLE h, const ULONG *pairs, ULONG count)
{
    static unsigned char in[4 + 256 * 8]; DWORD br; ULONG i;
    if (count > 256) return 1;
    *(ULONG *)in = count;
    for (i = 0; i < count * 2; i++) *(ULONG *)(in + 4 + i * 4) = pairs[i];
    if (!DeviceIoControl(h, IOCTL_MACH64_WRREGS, in, 4 + count * 8, NULL, 0, &br, NULL)) {
        OUTF("WRREGS(%lu) failed %lu\n", count, GetLastError()); return 1;
    }
    return 0;
}

static int map_combined(HANDLE h) { return dev_map(h, COMBINED_PHYS, COMBINED_SIZE); }

static void wait_idle(HANDLE h)
{
    ULONG i, v = 0;
    for (i = 0; i < 300000; i++) {
        if (dev_rd(h, REGOFF(R_GUI_STAT), &v)) return;
        if (!(v & GUI_ACTIVE)) return;
    }
    OUTF("  (engine still active, GUI_STAT=0x%08lx -- proceeding)\n", v);
}

/* Sub-rung 1: prove framebuffer mapping + block VRAM I/O (no engine). */
static int do_vram(void)
{
    HANDLE h = dev_open();
    if (h == INVALID_HANDLE_VALUE) return 1;
    if (map_combined(h)) { CloseHandle(h); return 1; }
    ULONG chip = 0; dev_rd(h, REGOFF(CONFIG_CHIP_ID), &chip);
    OUTF("via combined map: CONFIG_CHIP_ID=0x%08lx %s\n", chip, chip == 0x64004c4d ? "(OK)" : "(??)");
    ULONG offs[5] = { 0x400000, 0x600000, 0x700000, 0x780000, 0x7F0000 };
    ULONG k;
    for (k = 0; k < 5; k++) {
        ULONG off = offs[k], wr[8], rd[8], i;
        for (i = 0; i < 8; i++) wr[i] = 0xA5A50000u + (off >> 8) + i;
        if (dev_wrblk(h, off, wr, 32)) continue;
        for (i = 0; i < 8; i++) rd[i] = 0;
        if (dev_rdblk(h, off, rd, 32)) continue;
        int ok = 1; for (i = 0; i < 8; i++) if (rd[i] != wr[i]) ok = 0;
        OUTF("  VRAM 0x%07lx: %s (wr0=0x%08lx rd0=0x%08lx)\n", off, ok ? "OK backed" : "mismatch", wr[0], rd[0]);
    }
    CloseHandle(h);
    return 0;
}

/* Sub-rung 2: off-screen 2D solid fill via the engine, readback-verified.
 * Exact 14-register sequence from gld_render.c mach64_fill_rect (proven Lombard). */
static unsigned char g_fbbuf[4096];
static int do_fill(void)
{
    HANDLE h = dev_open();
    if (h == INVALID_HANDLE_VALUE) return 1;
    if (map_combined(h)) { CloseHandle(h); return 1; }
    ULONG chip = 0; dev_rd(h, REGOFF(CONFIG_CHIP_ID), &chip);
    if (chip != 0x64004c4d) OUTF("warn: CONFIG_CHIP_ID=0x%08lx\n", chip);

    ULONG off = FBTEST_OFF, w = 32, hgt = 32, pitch = 32, color = 0x00C0FFEE;
    ULONG opv = ((pitch / 8) << 22) | (off >> 3);

    /* pre-clear the target so a passing readback proves OUR fill, not stale data */
    { static ULONG zero[1024]; ULONG i; for (i = 0; i < 1024; i++) zero[i] = 0; dev_wrblk(h, off, zero, 4096); }

    wait_idle(h);
    ULONG pairs[] = {
        REGOFF(R_Z_CNTL), 0,               REGOFF(R_SCALE_3D_CNTL), 0,
        REGOFF(R_GUI_TRAJ_CNTL), 0x3,      REGOFF(R_DP_PIX_WIDTH), 0x60060666u,
        REGOFF(R_DP_FRGD_CLR), color,      REGOFF(R_DP_WRITE_MASK), 0xffffffffu,
        REGOFF(R_DP_MIX), 0x00070003u,     REGOFF(R_DP_SRC), 0x00000101u,
        REGOFF(R_SC_LEFT_RIGHT), 0x1fff0000u, REGOFF(R_SC_TOP_BOTTOM), 0x1fff0000u,
        REGOFF(R_CLR_CMP_CNTL), 0,         REGOFF(R_DST_OFF_PITCH), opv,
        REGOFF(R_DST_X_Y), 0,              REGOFF(R_DST_WIDTH_HEIGHT), (hgt << 16) | w,
    };
    if (dev_wrregs(h, pairs, 14)) { CloseHandle(h); return 1; }
    wait_idle(h);

    if (dev_rdblk(h, off, g_fbbuf, 4096)) { CloseHandle(h); return 1; }
    ULONG i, okc = 0;
    for (i = 0; i < 1024; i++) if ((*(ULONG *)(g_fbbuf + i * 4) & 0xffffff) == 0xC0FFEE) okc++;
    OUTF("fill 32x32 @0x%06lx color 0x%06lx: %lu/1024 px match  (px0=0x%08lx px500=0x%08lx)\n",
         off, color, okc, *(ULONG *)g_fbbuf, *(ULONG *)(g_fbbuf + 500 * 4));
    outs(okc == 1024 ? "RESULT: ENGINE FILL VERIFIED off-screen!\n" : "RESULT: fill incomplete/mismatch\n");
    CloseHandle(h);
    return okc == 1024 ? 0 : 1;
}

static int do_peek(void)
{
    HANDLE h = dev_open();
    if (h == INVALID_HANDLE_VALUE) return 1;
    if (dev_map(h, 0x81000000ULL, 0x1000)) { CloseHandle(h); return 1; }

    static const struct { const char *n; ULONG o; } regs[] = {
        { "CONFIG_CHIP_ID", CONFIG_CHIP_ID }, { "CONFIG_CNTL", CONFIG_CNTL },
        { "CONFIG_STAT0",   CONFIG_STAT0   }, { "MEM_CNTL",    MEM_CNTL    },
        { "EXT_MEM_CNTL",   EXT_MEM_CNTL   }, { "SCRATCH_REG0",SCRATCH_REG0},
    };
    ULONG i, chip = 0;
    for (i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        ULONG v = 0;
        if (dev_rd(h, regs[i].o, &v)) { CloseHandle(h); return 1; }
        OUTF("  %-14s [0x%04lx] = 0x%08lx\n", regs[i].n, regs[i].o, v);
        if (regs[i].o == CONFIG_CHIP_ID) chip = v;
    }
    ULONG type = chip & 0xffff, rev = (chip >> 24) & 0xff;
    OUTF("\nCHIP_TYPE=0x%04lx ('%c%c')  REV=0x%02lx\n", type, (type>>8)&0xff, type&0xff, rev);
    if (type == 0x4c4d) outs("RESULT: MATCH -- Rage Mobility-P (\"LM\"). Kernel register access works!\n");
    else                outs("RESULT: unexpected chip id -- check mapping/offsets.\n");
    CloseHandle(h);
    return (type == 0x4c4d) ? 0 : 1;
}

void _mainstart(void)
{
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    char *argv[8];
    int argc = split_args(GetCommandLineA(), argv, 8);
    int rc = 0;

    if (argc < 2) { outs("usage: kmdtest install|start|stop|remove|peek|vram|fill|rd|wr|auto ...\n"); ExitProcess(2); }

    if      (streq(argv[1], "install") && argc >= 3) rc = svc_install(argv[2]);
    else if (streq(argv[1], "start"))                rc = svc_ctl(1, 0);
    else if (streq(argv[1], "stop"))                 rc = svc_ctl(0, 0);
    else if (streq(argv[1], "remove"))               rc = svc_ctl(0, 1);
    else if (streq(argv[1], "peek"))                 rc = do_peek();
    else if (streq(argv[1], "vram"))                 rc = do_vram();
    else if (streq(argv[1], "fill"))                 rc = do_fill();
    else if (streq(argv[1], "auto") && argc >= 3) {
        rc = svc_install(argv[2]); if (!rc) rc = svc_ctl(1, 0); if (!rc) rc = do_peek();
    }
    else if (streq(argv[1], "rd") && argc >= 3) {
        HANDLE h = dev_open();
        if (h == INVALID_HANDLE_VALUE) rc = 1;
        else { if (!dev_map(h, 0x81000000ULL, 0x1000)) { ULONG v=0; if (!dev_rd(h, parse_num(argv[2]), &v)) OUTF("[0x%04lx] = 0x%08lx\n", parse_num(argv[2]), v); else rc=1; } CloseHandle(h); }
    }
    else if (streq(argv[1], "wr") && argc >= 4) {
        HANDLE h = dev_open();
        if (h == INVALID_HANDLE_VALUE) rc = 1;
        else { if (!dev_map(h, 0x81000000ULL, 0x1000)) { if (dev_wr(h, parse_num(argv[2]), parse_num(argv[3]))) rc=1; else outs("wrote ok\n"); } CloseHandle(h); }
    }
    else { outs("bad args\n"); rc = 2; }

    ExitProcess(rc);
}
