/*
 * mach64kmd.c - minimal Windows 2000 kernel helper for the mach64
 * (ATI Rage Mobility-P) on the Compaq Armada e500.
 *
 * Phase-1 register access: maps a device MMIO aperture with MmMapIoSpace() (which
 * has none of the user-mode \Device\PhysicalMemory "above RAM" restriction that
 * blocked win2k/tools/mach64_peek) and exposes read/write-by-offset IOCTLs.  This
 * is the Win2000 analog of the OS X kext's register/ring MMIO layer.
 *
 * DMA (contiguous buffer alloc + bus-master kick) comes later; this v1 is just
 * the register window so we can confirm CONFIG_CHIP_ID and do a scratch-register
 * write round-trip (read-before-write / off-screen-before-visible discipline).
 *
 * Legacy NT driver, cross-built from macOS with MinGW-w64 (ships ddk/ntddk.h +
 * libntoskrnl/libhal).  Win2000 loads unsigned drivers, so no signing needed.
 *
 * Build: see win2k/kmd/Makefile.  Device: \\.\Mach64Kmd
 */

#include <ddk/ntddk.h>

#define NT_DEVICE_NAME  L"\\Device\\Mach64Kmd"
#define DOS_DEVICE_NAME L"\\DosDevices\\Mach64Kmd"

/* IOCTLs (must match win2k/kmd/kmdtest.c) */
#define MACH64_TYPE 0x8000
#define IOCTL_MACH64_MAP   CTL_CODE(MACH64_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_UNMAP CTL_CODE(MACH64_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_RD32  CTL_CODE(MACH64_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_WR32  CTL_CODE(MACH64_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
/* block VRAM read/write (dword granular) + atomic batch register write */
#define IOCTL_MACH64_RDBLK  CTL_CODE(MACH64_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_WRBLK  CTL_CODE(MACH64_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MACH64_WRREGS CTL_CODE(MACH64_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
/* pseudo-DMA submit: feed a decoded (mapoffset,value) stream to the engine with
 * FIFO flow control (drain every 16 writes) + idle waits -- for >16-write batches
 * like a 3D triangle. Input: count, gui_stat_mapoff, fifo_stat_mapoff, then pairs. */
#define IOCTL_MACH64_SUBMIT CTL_CODE(MACH64_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
/* bus-master DMA submit: copy an LE command stream into contiguous DMA memory,
 * build the descriptor ring, activate bus-mastering, kick, wait, restore ATI's
 * BUS_CNTL/SRC_CNTL. Input: reg_bias, stream_len, stream bytes (<=4092). */
#define IOCTL_MACH64_DMASUBMIT CTL_CODE(MACH64_TYPE, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* mach64 bus-master registers (common/mach64_regs.h) */
#define M64_BUS_CNTL            0x04a0
#define M64_BUS_MASTER_DIS      (1u << 6)
#define M64_BUS_EXT_REG_EN      (1u << 27)
#define M64_SRC_CNTL            0x05b4
#define M64_SRC_BM_ENABLE       (1u << 8)
#define M64_SRC_BM_SYNC         (1u << 9)
#define M64_SRC_BM_OP_SYS_TO_REG (3u << 10)
#define M64_BM_GUI_TABLE_CMD    0x064c
#define M64_DST_HEIGHT_WIDTH    0x0518
#define M64_FIFO_STAT           0x0710
#define M64_GUI_STAT            0x0738
#define M64_APERTURE_BM_ADDR    0x7ffe48u   /* APERTURE_OFFSET(0x7ff800)+BM_ADDR(0x648) */
#define M64_DMA_HOLD_OFFSET     (1u << 30)
#define M64_DMA_EOL             (1u << 31)

#define MACH64_MAXBLK  0x10000u   /* 64KB per block IOCTL */
#define MACH64_MAXREGS 256u       /* pairs per WRREGS      */
#define MACH64_MAXMAP  0x02000000u /* allow FB(16MB)+regs in one contiguous map */

/* IOCTL payloads (read fields individually to avoid aggregate-copy memcpy under
 * -nostdlib). MAP_IN: phys(8) + size(4). RD_IN: off(4)->out ULONG. WR_IN: off,val. */
typedef struct _DEVEXT {
    PVOID mapped; ULONG mapsize;
    PVOID dma_va;  ULONG dma_size;         /* contiguous DMA block (cached; x86 snoops) */
    PVOID ring_va; ULONG ring_phys;        /* 16KB-aligned descriptor ring             */
    PVOID cmd_va;  ULONG cmd_phys;         /* command-stream buffer                     */
} DEVEXT;

static void mach64_unmap(DEVEXT *ex)
{
    if (ex->mapped) { MmUnmapIoSpace(ex->mapped, ex->mapsize); ex->mapped = NULL; ex->mapsize = 0; }
}

static NTSTATUS NTAPI DispCreateClose(PDEVICE_OBJECT dev, PIRP irp)
{
    UNREFERENCED_PARAMETER(dev);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI DispIoctl(PDEVICE_OBJECT dev, PIRP irp)
{
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(irp);
    DEVEXT *ex = (DEVEXT *)dev->DeviceExtension;
    ULONG inlen  = s->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outlen = s->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG code   = s->Parameters.DeviceIoControl.IoControlCode;
    PUCHAR buf   = (PUCHAR)irp->AssociatedIrp.SystemBuffer;   /* buffered IO */
    NTSTATUS st  = STATUS_INVALID_DEVICE_REQUEST;
    ULONG info   = 0;

    switch (code) {
    case IOCTL_MACH64_MAP: {
        if (inlen < 12) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONGLONG phys = *(ULONGLONG UNALIGNED *)(buf + 0);
        ULONG     size = *(ULONG     UNALIGNED *)(buf + 8);
        if (size == 0 || size > MACH64_MAXMAP) { st = STATUS_INVALID_PARAMETER; break; }
        mach64_unmap(ex);
        PHYSICAL_ADDRESS pa; pa.QuadPart = (LONGLONG)phys;
        ex->mapped = MmMapIoSpace(pa, size, MmNonCached);
        if (!ex->mapped) { st = STATUS_INSUFFICIENT_RESOURCES; break; }
        ex->mapsize = size;
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_UNMAP:
        mach64_unmap(ex);
        st = STATUS_SUCCESS;
        break;
    case IOCTL_MACH64_RD32: {
        if (inlen < 4 || outlen < 4) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG off = *(ULONG UNALIGNED *)buf;
        if (!ex->mapped || off + 4 > ex->mapsize) { st = STATUS_INVALID_PARAMETER; break; }
        ULONG v = READ_REGISTER_ULONG((PULONG)((PUCHAR)ex->mapped + off));
        *(ULONG UNALIGNED *)buf = v;
        info = 4;
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_WR32: {
        if (inlen < 8) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG off = *(ULONG UNALIGNED *)(buf + 0);
        ULONG val = *(ULONG UNALIGNED *)(buf + 4);
        if (!ex->mapped || off + 4 > ex->mapsize) { st = STATUS_INVALID_PARAMETER; break; }
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)ex->mapped + off), val);
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_RDBLK: {
        if (inlen < 8) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG off = *(ULONG UNALIGNED *)(buf + 0);
        ULONG len = *(ULONG UNALIGNED *)(buf + 4);
        if (len == 0 || (len & 3) || len > MACH64_MAXBLK || len > outlen) { st = STATUS_INVALID_PARAMETER; break; }
        if (!ex->mapped || off + len > ex->mapsize || (off & 3)) { st = STATUS_INVALID_PARAMETER; break; }
        PUCHAR src = (PUCHAR)ex->mapped + off;
        ULONG i;
        for (i = 0; i < len; i += 4)
            *(ULONG UNALIGNED *)(buf + i) = READ_REGISTER_ULONG((PULONG)(src + i));
        info = len;
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_WRBLK: {
        if (inlen < 8) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG off = *(ULONG UNALIGNED *)(buf + 0);
        ULONG len = *(ULONG UNALIGNED *)(buf + 4);
        if (len == 0 || (len & 3) || len > MACH64_MAXBLK || inlen < 8 + len) { st = STATUS_INVALID_PARAMETER; break; }
        if (!ex->mapped || off + len > ex->mapsize || (off & 3)) { st = STATUS_INVALID_PARAMETER; break; }
        PUCHAR dst = (PUCHAR)ex->mapped + off;
        PUCHAR data = buf + 8;
        ULONG i;
        for (i = 0; i < len; i += 4)
            WRITE_REGISTER_ULONG((PULONG)(dst + i), *(ULONG UNALIGNED *)(data + i));
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_WRREGS: {
        if (inlen < 4) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG count = *(ULONG UNALIGNED *)buf;
        if (count == 0 || count > MACH64_MAXREGS || inlen < 4 + count * 8) { st = STATUS_INVALID_PARAMETER; break; }
        if (!ex->mapped) { st = STATUS_INVALID_PARAMETER; break; }
        PUCHAR pairs = buf + 4;
        ULONG i; int bad = 0;
        for (i = 0; i < count; i++) {              /* validate all before touching HW */
            ULONG ro = *(ULONG UNALIGNED *)(pairs + i * 8);
            if (ro + 4 > ex->mapsize || (ro & 3)) { bad = 1; break; }
        }
        if (bad) { st = STATUS_INVALID_PARAMETER; break; }
        {   /* apply the whole burst at DISPATCH_LEVEL: on this uniprocessor box that
             * keeps ATI's live driver from interleaving register writes mid-sequence */
            KIRQL old;
            KeRaiseIrql(DISPATCH_LEVEL, &old);
            for (i = 0; i < count; i++) {
                ULONG ro = *(ULONG UNALIGNED *)(pairs + i * 8 + 0);
                ULONG v  = *(ULONG UNALIGNED *)(pairs + i * 8 + 4);
                WRITE_REGISTER_ULONG((PULONG)((PUCHAR)ex->mapped + ro), v);
            }
            KeLowerIrql(old);
        }
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_SUBMIT: {
        if (inlen < 12) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG count    = *(ULONG UNALIGNED *)(buf + 0);
        ULONG gui_off  = *(ULONG UNALIGNED *)(buf + 4);
        ULONG fifo_off = *(ULONG UNALIGNED *)(buf + 8);
        if (count == 0 || count > MACH64_MAXREGS || inlen < 12 + count * 8) { st = STATUS_INVALID_PARAMETER; break; }
        if (!ex->mapped || gui_off + 4 > ex->mapsize || fifo_off + 4 > ex->mapsize) { st = STATUS_INVALID_PARAMETER; break; }
        PUCHAR pairs = buf + 12;
        ULONG i; int bad = 0;
        for (i = 0; i < count; i++) {
            ULONG ro = *(ULONG UNALIGNED *)(pairs + i * 8);
            if (ro + 4 > ex->mapsize || (ro & 3)) { bad = 1; break; }
        }
        if (bad) { st = STATUS_INVALID_PARAMETER; break; }
        {
            PUCHAR m = (PUCHAR)ex->mapped;
            PULONG fifo = (PULONG)(m + fifo_off);
            PULONG gui  = (PULONG)(m + gui_off);
            ULONG t; int fifoleft = 0;
            KIRQL old;
            KeRaiseIrql(DISPATCH_LEVEL, &old);
            /* wait engine idle first: FIFO empty (==0), then GUI not active */
            for (t = 0; t < 100000 && (READ_REGISTER_ULONG(fifo) & 0xffff); t++) KeStallExecutionProcessor(1);
            for (t = 0; t < 100000 && (READ_REGISTER_ULONG(gui) & 1);      t++) KeStallExecutionProcessor(1);
            for (i = 0; i < count; i++) {
                if (fifoleft == 0) {                  /* drain FIFO every 16 writes */
                    for (t = 0; t < 100000 && (READ_REGISTER_ULONG(fifo) & 0xffff); t++) KeStallExecutionProcessor(1);
                    fifoleft = 16;
                }
                {
                    ULONG ro = *(ULONG UNALIGNED *)(pairs + i * 8 + 0);
                    ULONG v  = *(ULONG UNALIGNED *)(pairs + i * 8 + 4);
                    WRITE_REGISTER_ULONG((PULONG)(m + ro), v);
                }
                fifoleft--;
            }
            /* wait for the drawn primitive to complete before we return */
            for (t = 0; t < 100000 && (READ_REGISTER_ULONG(fifo) & 0xffff); t++) KeStallExecutionProcessor(1);
            for (t = 0; t < 100000 && (READ_REGISTER_ULONG(gui) & 1);      t++) KeStallExecutionProcessor(1);
            KeLowerIrql(old);
        }
        st = STATUS_SUCCESS;
        break;
    }
    case IOCTL_MACH64_DMASUBMIT: {
        if (inlen < 8) { st = STATUS_BUFFER_TOO_SMALL; break; }
        ULONG reg_bias = *(ULONG UNALIGNED *)(buf + 0);
        ULONG slen     = *(ULONG UNALIGNED *)(buf + 4);
        if (slen == 0 || (slen & 3) || slen > 4092 || inlen < 8 + slen) { st = STATUS_INVALID_PARAMETER; break; }
        if (!ex->mapped || reg_bias + 0x1000 > ex->mapsize) { st = STATUS_INVALID_PARAMETER; break; }

        /* lazily allocate the DMA block: 64KB contiguous, <4GB. Carve a 16KB-aligned
         * ring + a command buffer. Cached is fine on x86 (bus-master snoops). */
        if (!ex->dma_va) {
            PHYSICAL_ADDRESS hi; hi.QuadPart = 0xFFFFFFFF;
            ex->dma_va = MmAllocateContiguousMemory(0x10000, hi);
            if (!ex->dma_va) { st = STATUS_INSUFFICIENT_RESOURCES; break; }
            ex->dma_size = 0x10000;
            PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(ex->dma_va);
            ULONG base = pa.LowPart;
            ULONG ringoff = (0x4000 - (base & 0x3fff)) & 0x3fff;   /* 16KB-align the ring */
            ex->ring_va   = (PUCHAR)ex->dma_va + ringoff;
            ex->ring_phys = base + ringoff;
            ex->cmd_va    = (PUCHAR)ex->ring_va + 0x4000;          /* cmdbuf after 16KB ring */
            ex->cmd_phys  = ex->ring_phys + 0x4000;
        }

        {   /* copy stream -> cmdbuf, build the single EOL descriptor */
            PUCHAR s = buf + 8, d = (PUCHAR)ex->cmd_va; ULONG i;
            for (i = 0; i < slen; i++) d[i] = s[i];
            PULONG r = (PULONG)ex->ring_va;
            r[0] = M64_APERTURE_BM_ADDR;
            r[1] = ex->cmd_phys;
            r[2] = slen | M64_DMA_HOLD_OFFSET | M64_DMA_EOL;
            r[3] = 0;
        }
        KeMemoryBarrier();      /* CPU stores globally visible before the kick (x86 WB) */

        {
            PUCHAR m = (PUCHAR)ex->mapped + reg_bias;
            KIRQL old; ULONG t, bus_s, src_s;
            KeRaiseIrql(DISPATCH_LEVEL, &old);
            /* quiesce the shared engine */
            for (t = 0; t < 100000 && (READ_REGISTER_ULONG((PULONG)(m + M64_FIFO_STAT)) & 0xffff); t++) KeStallExecutionProcessor(1);
            for (t = 0; t < 100000 && (READ_REGISTER_ULONG((PULONG)(m + M64_GUI_STAT)) & 1);       t++) KeStallExecutionProcessor(1);
            bus_s = READ_REGISTER_ULONG((PULONG)(m + M64_BUS_CNTL));
            src_s = READ_REGISTER_ULONG((PULONG)(m + M64_SRC_CNTL));
            /* activate bus-mastering, point at our ring, arm system->reg, kick */
            WRITE_REGISTER_ULONG((PULONG)(m + M64_BUS_CNTL), (bus_s & ~M64_BUS_MASTER_DIS) | M64_BUS_EXT_REG_EN);
            WRITE_REGISTER_ULONG((PULONG)(m + M64_BM_GUI_TABLE_CMD), ex->ring_phys);   /* CIRCULAR_BUF_SIZE_16KB = 0 */
            WRITE_REGISTER_ULONG((PULONG)(m + M64_SRC_CNTL), M64_SRC_BM_ENABLE | M64_SRC_BM_SYNC | M64_SRC_BM_OP_SYS_TO_REG);
            WRITE_REGISTER_ULONG((PULONG)(m + M64_DST_HEIGHT_WIDTH), 0);               /* kicks the transfer */
            /* wait for completion */
            for (t = 0; t < 300000 && (READ_REGISTER_ULONG((PULONG)(m + M64_FIFO_STAT)) & 0xffff); t++) KeStallExecutionProcessor(1);
            for (t = 0; t < 300000 && (READ_REGISTER_ULONG((PULONG)(m + M64_GUI_STAT)) & 1);       t++) KeStallExecutionProcessor(1);
            /* restore ATI's bus-master control so its next 2D op doesn't read our ring */
            WRITE_REGISTER_ULONG((PULONG)(m + M64_SRC_CNTL), src_s);
            WRITE_REGISTER_ULONG((PULONG)(m + M64_BUS_CNTL), bus_s);
            KeLowerIrql(old);
        }
        st = STATUS_SUCCESS;
        break;
    }
    default:
        break;
    }

    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return st;
}

static VOID NTAPI DrvUnload(PDRIVER_OBJECT drv)
{
    UNICODE_STRING dos;
    RtlInitUnicodeString(&dos, DOS_DEVICE_NAME);
    IoDeleteSymbolicLink(&dos);
    if (drv->DeviceObject) {
        DEVEXT *ex = (DEVEXT *)drv->DeviceObject->DeviceExtension;
        mach64_unmap(ex);
        if (ex->dma_va) { MmFreeContiguousMemory(ex->dma_va); ex->dma_va = NULL; }
        IoDeleteDevice(drv->DeviceObject);
    }
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg)
{
    UNICODE_STRING nt, dos;
    PDEVICE_OBJECT dev;
    NTSTATUS st;
    UNREFERENCED_PARAMETER(reg);

    RtlInitUnicodeString(&nt, NT_DEVICE_NAME);
    RtlInitUnicodeString(&dos, DOS_DEVICE_NAME);

    st = IoCreateDevice(drv, sizeof(DEVEXT), &nt, FILE_DEVICE_UNKNOWN, 0, FALSE, &dev);
    if (!NT_SUCCESS(st)) return st;

    ((DEVEXT *)dev->DeviceExtension)->mapped = NULL;
    ((DEVEXT *)dev->DeviceExtension)->mapsize = 0;

    st = IoCreateSymbolicLink(&dos, &nt);
    if (!NT_SUCCESS(st)) { IoDeleteDevice(dev); return st; }

    drv->MajorFunction[IRP_MJ_CREATE]         = DispCreateClose;
    drv->MajorFunction[IRP_MJ_CLOSE]          = DispCreateClose;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispIoctl;
    drv->DriverUnload = DrvUnload;
    dev->Flags |= DO_BUFFERED_IO;

    return STATUS_SUCCESS;
}
