/*
 * mach64_peek.c - Windows 2000 user-mode MMIO register peek for the mach64
 * (ATI Rage Mobility-P, DEV_4C4D) on the Compaq Armada e500.  READ-ONLY.
 *
 * Maps \Device\PhysicalMemory (open to admins on Win2000, before the XP SP2
 * lockdown) and reads the mach64 config registers to confirm register access,
 * the Phase-0 aperture map (MMIO BAR @ 0x81000000), and that common/mach64_regs.h
 * offsets apply to the "LM" chip.  x86 + LE register window => read dwords direct.
 *
 * *** Built with NO C RUNTIME *** so it loads on Win2000.  Modern MinGW-w64's CRT
 * startup imports EncodePointer/DecodePointer (KERNEL32, XP SP2+), which don't
 * exist on Win2000 -> "entry point not found".  We supply our own entry point and
 * use only kernel32/user32/ntdll.
 *
 * Build (cross, on the Mac):
 *   i686-w64-mingw32-gcc -O2 -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs \
 *       -e _mainstart -o mach64_peek.exe mach64_peek.c -lkernel32 -luser32
 *
 * Usage:  mach64_peek.exe [physbase_hex] [regbias_hex]
 *   physbase default 0x81000000 (the 4KB MMIO register BAR)
 *   regbias  default 0  (register R read at mapped + regbias + R)
 *   e.g. registers at top of an 8MB-style FB aperture:
 *        mach64_peek.exe 0x807ff000 0x800
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ---- minimal NT definitions (no DDK) ---- */
typedef LONG NTSTATUS;
#define NT_SUCCESS(s) ((s) >= 0)
typedef struct _UNICODE_STRING { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING;
typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length; HANDLE RootDirectory; UNICODE_STRING *ObjectName;
    ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES;
#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x40
#endif
#ifndef SECTION_MAP_READ
#define SECTION_MAP_READ 0x0004
#endif
#ifndef SECTION_MAP_WRITE
#define SECTION_MAP_WRITE 0x0002
#endif
#define VIEW_SHARE 1

typedef NTSTATUS (NTAPI *pNtOpenSection)(PHANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef NTSTATUS (NTAPI *pNtMapViewOfSection)(HANDLE, HANDLE, PVOID *, ULONG_PTR,
        SIZE_T, LARGE_INTEGER *, SIZE_T *, DWORD, ULONG, ULONG);
typedef NTSTATUS (NTAPI *pNtUnmapViewOfSection)(HANDLE, PVOID);

/* wsprintfA lives in user32.dll (present on Win2000); cdecl variadic. */
__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);

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

/* tiny hex/dec parser: handles optional 0x prefix */
static ULONG parse_num(const char *s)
{
    ULONG v = 0; int hex = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { hex = 1; s += 2; }
    for (; *s; s++) {
        char c = *s; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (hex) v = (v << 4) | d; else v = v * 10 + d;
    }
    return v;
}

/* split GetCommandLineA into up to argmax tokens (space-separated); returns count.
 * argv[0] is the program name. Simple: no quote handling needed for our args. */
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

void _mainstart(void)
{
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);

    ULONG physbase = 0x81000000UL, regbias = 0;
    char *argv[8];
    int argc = split_args(GetCommandLineA(), argv, 8);
    if (argc > 1) physbase = parse_num(argv[1]);
    if (argc > 2) regbias  = parse_num(argv[2]);

    HMODULE nt = GetModuleHandleA("ntdll.dll");
    pNtOpenSection        NtOpenSection        = (pNtOpenSection)       GetProcAddress(nt, "NtOpenSection");
    pNtMapViewOfSection   NtMapViewOfSection   = (pNtMapViewOfSection)  GetProcAddress(nt, "NtMapViewOfSection");
    pNtUnmapViewOfSection NtUnmapViewOfSection = (pNtUnmapViewOfSection)GetProcAddress(nt, "NtUnmapViewOfSection");
    if (!NtOpenSection || !NtMapViewOfSection) { outs("ERROR: no Nt* in ntdll\n"); ExitProcess(2); }

    WCHAR name[] = L"\\Device\\PhysicalMemory";
    int wl = 0; while (name[wl]) wl++;
    UNICODE_STRING us; us.Buffer = name;
    us.Length = (USHORT)(wl * 2); us.MaximumLength = (USHORT)(us.Length + 2);

    OBJECT_ATTRIBUTES oa;
    oa.Length = sizeof(oa); oa.RootDirectory = 0; oa.ObjectName = &us;
    oa.Attributes = OBJ_CASE_INSENSITIVE; oa.SecurityDescriptor = 0; oa.SecurityQualityOfService = 0;

    HANDLE hSec = 0;
    NTSTATUS st = NtOpenSection(&hSec, SECTION_MAP_READ | SECTION_MAP_WRITE, &oa);
    if (!NT_SUCCESS(st))  /* fall back to read-only section access */
        st = NtOpenSection(&hSec, SECTION_MAP_READ, &oa);
    if (!NT_SUCCESS(st)) {
        OUTF("ERROR: NtOpenSection failed 0x%08lX (0xC0000022=ACCESS_DENIED need admin)\n", (ULONG)st);
        ExitProcess(3);
    }

    SIZE_T viewSize = 0x1000;
    LARGE_INTEGER off; off.QuadPart = (LONGLONG)physbase;
    PVOID base = 0;
    /* physical-memory mapping is treated as write-capable; try RW first */
    st = NtMapViewOfSection(hSec, GetCurrentProcess(), &base, 0, viewSize, &off, &viewSize, VIEW_SHARE, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(st))
        st = NtMapViewOfSection(hSec, GetCurrentProcess(), &base, 0, viewSize, &off, &viewSize, VIEW_SHARE, 0, PAGE_READONLY);
    if (!NT_SUCCESS(st)) { OUTF("ERROR: NtMapViewOfSection(phys=0x%08lX) failed 0x%08lX\n", physbase, (ULONG)st); ExitProcess(4); }

    volatile unsigned char *p = (volatile unsigned char *)base + regbias;
    OUTF("Mapped phys 0x%08lX (bias 0x%lX) at %p\n", physbase, regbias, base);

    static const struct { const char *n; unsigned o; } regs[] = {
        { "CONFIG_CHIP_ID", CONFIG_CHIP_ID }, { "CONFIG_CNTL", CONFIG_CNTL },
        { "CONFIG_STAT0",   CONFIG_STAT0   }, { "MEM_CNTL",    MEM_CNTL    },
        { "EXT_MEM_CNTL",   EXT_MEM_CNTL   }, { "SCRATCH_REG0",SCRATCH_REG0},
    };
    unsigned i, chip_id = 0;
    for (i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        unsigned v = *(volatile unsigned *)(p + regs[i].o);
        OUTF("  %-14s [0x%04x] = 0x%08x\n", regs[i].n, regs[i].o, v);
        if (regs[i].o == CONFIG_CHIP_ID) chip_id = v;
    }

    unsigned type = chip_id & 0xffff, rev = (chip_id >> 24) & 0xff;
    OUTF("\nCHIP_TYPE=0x%04x ('%c%c')  REV=0x%02x\n", type, (type>>8)&0xff, type&0xff, rev);
    if (type == 0x4c4d)
        outs("RESULT: MATCH -- Rage Mobility-P (\"LM\"). Aperture + register offsets confirmed.\n");
    else if (chip_id == 0xffffffff || chip_id == 0)
        outs("RESULT: no device response -- wrong base/bias; try another mapping.\n");
    else
        outs("RESULT: read an id but not 0x4c4d -- check base/bias/mapping.\n");

    if (NtUnmapViewOfSection) NtUnmapViewOfSection(GetCurrentProcess(), base);
    CloseHandle(hSec);
    ExitProcess(type == 0x4c4d ? 0 : 1);
}
