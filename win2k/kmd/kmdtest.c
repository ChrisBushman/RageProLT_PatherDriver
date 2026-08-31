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

/* mach64 register offsets (docs/dri-reference/drm_mach64_drv.h) */
#define CONFIG_CHIP_ID 0x04e0
#define CONFIG_CNTL    0x04dc
#define CONFIG_STAT0   0x04e4
#define MEM_CNTL       0x04b0
#define EXT_MEM_CNTL   0x04ac
#define SCRATCH_REG0   0x0480

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

    if (argc < 2) { outs("usage: kmdtest install|start|stop|remove|peek|rd|wr|auto ...\n"); ExitProcess(2); }

    if      (streq(argv[1], "install") && argc >= 3) rc = svc_install(argv[2]);
    else if (streq(argv[1], "start"))                rc = svc_ctl(1, 0);
    else if (streq(argv[1], "stop"))                 rc = svc_ctl(0, 0);
    else if (streq(argv[1], "remove"))               rc = svc_ctl(0, 1);
    else if (streq(argv[1], "peek"))                 rc = do_peek();
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
