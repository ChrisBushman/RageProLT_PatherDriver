/*
 * mach64_winsys.c - user-mode winsys over the win2k/kmd kernel helper.
 * See mach64_winsys.h. Wraps \\.\Mach64Kmd IOCTLs; no CRT dependency.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include "mach64_winsys.h"

/* IOCTL contract -- must match win2k/kmd/mach64kmd.c */
#define MACH64_TYPE 0x8000
#define CTL(f) CTL_CODE(MACH64_TYPE, (f), METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_MAP       CTL(0x800)
#define IOCTL_MACH64_RD32      CTL(0x802)
#define IOCTL_MACH64_WR32      CTL(0x803)
#define IOCTL_MACH64_RDBLK     CTL(0x804)
#define IOCTL_MACH64_WRBLK     CTL(0x805)
#define IOCTL_MACH64_DMASUBMIT CTL(0x808)

#define COMBINED_PHYS 0x80000000ULL
#define COMBINED_SIZE 0x01001000UL     /* 16MB VRAM + 4KB regs (contiguous) */
#define REG_BIAS      0x01000000UL     /* register R at map offset REG_BIAS+R */
#define VRAM_SIZE     0x00800000UL     /* 8MB (confirmed on the Armada)       */

struct m64_winsys { HANDLE h; };

static m64_winsys g_ws;   /* single device; no malloc needed */

m64_winsys *m64ws_open(void)
{
    HANDLE h = CreateFileA("\\\\.\\Mach64Kmd", GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    /* MAP the combined FB+regs window */
    unsigned char in[16]; DWORD br;
    *(ULONGLONG *)(in + 0) = COMBINED_PHYS;
    *(ULONG *)(in + 8) = COMBINED_SIZE;
    if (!DeviceIoControl(h, IOCTL_MACH64_MAP, in, 12, NULL, 0, &br, NULL)) {
        CloseHandle(h); return NULL;
    }
    g_ws.h = h;
    return &g_ws;
}

void m64ws_close(m64_winsys *ws)
{
    if (ws && ws->h) { CloseHandle(ws->h); ws->h = NULL; }
}

unsigned m64ws_vram_size(m64_winsys *ws) { (void)ws; return VRAM_SIZE; }

unsigned m64ws_reg_read(m64_winsys *ws, unsigned reg)
{
    ULONG off = REG_BIAS + reg, v = 0; DWORD br;
    DeviceIoControl(ws->h, IOCTL_MACH64_RD32, &off, 4, &v, 4, &br, NULL);
    return v;
}

void m64ws_reg_write(m64_winsys *ws, unsigned reg, unsigned val)
{
    unsigned char in[8]; DWORD br;
    *(ULONG *)(in + 0) = REG_BIAS + reg; *(ULONG *)(in + 4) = val;
    DeviceIoControl(ws->h, IOCTL_MACH64_WR32, in, 8, NULL, 0, &br, NULL);
}

/* Block VRAM I/O, chunked in 4KB pieces so any size (e.g. a texture) works safely. */
#define WS_CHUNK 0x1000

int m64ws_read_vram(m64_winsys *ws, unsigned off, void *dst, unsigned len)
{
    unsigned char *d = (unsigned char *)dst; unsigned done = 0; DWORD br;
    while (done < len) {
        unsigned n = len - done; if (n > WS_CHUNK) n = WS_CHUNK;
        unsigned char in[8];
        *(ULONG *)(in + 0) = off + done; *(ULONG *)(in + 4) = n;
        if (!DeviceIoControl(ws->h, IOCTL_MACH64_RDBLK, in, 8, d + done, n, &br, NULL)) return 1;
        done += n;
    }
    return 0;
}

int m64ws_write_vram(m64_winsys *ws, unsigned off, const void *src, unsigned len)
{
    const unsigned char *s = (const unsigned char *)src; unsigned done = 0; DWORD br;
    static unsigned char in[8 + WS_CHUNK];
    while (done < len) {
        unsigned n = len - done, i; if (n > WS_CHUNK) n = WS_CHUNK;
        *(ULONG *)(in + 0) = off + done; *(ULONG *)(in + 4) = n;
        for (i = 0; i < n; i++) in[8 + i] = s[done + i];
        if (!DeviceIoControl(ws->h, IOCTL_MACH64_WRBLK, in, 8 + n, NULL, 0, &br, NULL)) return 1;
        done += n;
    }
    return 0;
}

int m64ws_submit(m64_winsys *ws, const m64_cmdbuf *cb)
{
    static unsigned char in[8 + 4096]; DWORD br;
    unsigned len = (unsigned)m64_cb_write_le(cb, in + 8);   /* pack LE after the header */
    if (len == 0 || len > 4092) return 1;
    *(ULONG *)(in + 0) = REG_BIAS;
    *(ULONG *)(in + 4) = len;
    return DeviceIoControl(ws->h, IOCTL_MACH64_DMASUBMIT, in, 8 + len, NULL, 0, &br, NULL) ? 0 : 1;
}
