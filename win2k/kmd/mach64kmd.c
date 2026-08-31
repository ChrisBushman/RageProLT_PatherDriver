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

#define MACH64_MAXBLK  0x10000u   /* 64KB per block IOCTL */
#define MACH64_MAXREGS 256u       /* pairs per WRREGS      */
#define MACH64_MAXMAP  0x02000000u /* allow FB(16MB)+regs in one contiguous map */

/* IOCTL payloads (read fields individually to avoid aggregate-copy memcpy under
 * -nostdlib). MAP_IN: phys(8) + size(4). RD_IN: off(4)->out ULONG. WR_IN: off,val. */
typedef struct _DEVEXT { PVOID mapped; ULONG mapsize; } DEVEXT;

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
        mach64_unmap((DEVEXT *)drv->DeviceObject->DeviceExtension);
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
