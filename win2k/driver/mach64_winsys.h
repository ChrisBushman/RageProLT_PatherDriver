/*
 * mach64_winsys.h - user-mode "winsys" for the Rage Mobility-P: the device layer the
 * Mesa mach64 backend calls, wrapping the win2k/kmd kernel helper (\\.\Mach64Kmd).
 *
 * This is the Win2000 replacement for the DRI driver's device layer
 * (mach64_ioctl.c DMA submission + mach64_span.c framebuffer access): it submits
 * command streams built by common/mach64_emit via bus-master DMA, and reads/writes
 * VRAM and registers. Reusable by both CRT-free tools and the msvcrt Mesa DLL
 * (no CRT dependency; only kernel32 + common/mach64_emit).
 */
#ifndef MACH64_WINSYS_H
#define MACH64_WINSYS_H

#include "mach64_emit.h"   /* m64_cmdbuf (from ../../common) */

typedef struct m64_winsys m64_winsys;

/* Open \\.\Mach64Kmd and map the combined framebuffer(16MB)+registers window.
 * Returns NULL on failure (driver not loaded / access denied). */
m64_winsys *m64ws_open(void);
void        m64ws_close(m64_winsys *ws);

/* VRAM base (byte offset 0) and size in bytes, and the register-access bias. */
unsigned m64ws_vram_size(m64_winsys *ws);

/* Bus-master DMA a command stream (host-order dwords in cb; packed LE internally).
 * Returns 0 on success. Stream must be <= 4092 bytes (12-bit DMA count limit). */
int m64ws_submit(m64_winsys *ws, const m64_cmdbuf *cb);

/* Direct register + VRAM access (for state readback, span read/write, present). */
unsigned m64ws_reg_read (m64_winsys *ws, unsigned reg);
void     m64ws_reg_write(m64_winsys *ws, unsigned reg, unsigned val);
int m64ws_read_vram (m64_winsys *ws, unsigned off, void *dst, unsigned len);
int m64ws_write_vram(m64_winsys *ws, unsigned off, const void *src, unsigned len);

#endif /* MACH64_WINSYS_H */
