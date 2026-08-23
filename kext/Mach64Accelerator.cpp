/*
 * Mach64Accelerator.cpp - Rage LT Pro IOService provider (compile skeleton).
 * Builds against the Mac OS X 10.3 Kernel framework.
 *
 * NOTE on coexistence: Apple's ATIRagePro matches this same IOPCIDevice with
 * a positive probe score.  To actually attach, this driver's IOProbeScore
 * must exceed it (or Apple's driver must be prevented from matching).  That
 * is a load-time concern; this file only needs to COMPILE for now.
 */
#include "Mach64Accelerator.h"
#include "Mach64GLUserClient.h"
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(Mach64Accelerator, IOService)

bool Mach64Accelerator::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    fPCI = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCI) return false;

    fPCI->setMemoryEnable(true);
    fPCI->setBusMasterEnable(true);

    /* BAR layout (TODO verify on hardware via the device's assigned-addresses):
     *   BAR0 = linear framebuffer aperture, BAR2 = MMIO register aperture. */
    fFB   = fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    fRegs = fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if (!fRegs || !fFB) {
        IOLog("Mach64Accelerator: missing register/FB aperture\n");
        return false;
    }
    fRegs->retain();
    fFB->retain();

    /* Shared control page (type 2) + growable command buffer (type 4). */
    fControl = IOBufferMemoryDescriptor::withOptions(
        kIOMemoryKernelUserShared | kIODirectionInOut, 0x1000, PAGE_SIZE);
    fCmdBuf  = IOBufferMemoryDescriptor::withOptions(
        kIOMemoryKernelUserShared | kIODirectionInOut, 0x10000, PAGE_SIZE);
    if (!fControl || !fCmdBuf) return false;

    fStamp   = 0;
    /* Real values come from reading CONFIG_CHIP_ID / memory config; the plugin
     * only inspects bit 1 of config0 (see gldCreateContext). */
    fConfig0 = 0;
    fConfig1 = 0x00800000;   /* 8 MB VRAM (verified on this Lombard)         */

    IOLog("Mach64Accelerator: started on %s\n", fPCI->getName());
    registerService();
    return true;
}

void Mach64Accelerator::stop(IOService *provider)
{
    super::stop(provider);
}

void Mach64Accelerator::free(void)
{
    if (fRegs)    { fRegs->release();    fRegs = 0; }
    if (fFB)      { fFB->release();      fFB = 0; }
    if (fControl) { fControl->release(); fControl = 0; }
    if (fCmdBuf)  { fCmdBuf->release();  fCmdBuf = 0; }
    super::free();
}

/* ---- regions ---- */
IOMemoryDescriptor *Mach64Accelerator::allocSurface(Mach64GLUserClient *c)
{
    (void)c;
    /* TODO: carve a color/depth surface out of VRAM.  Placeholder: whole FB. */
    return fFB;
}

IOMemoryDescriptor *Mach64Accelerator::commandBuffer(Mach64GLUserClient *c)
{
    (void)c;
    return fCmdBuf;
}

/* ---- config / context ---- */
UInt32 Mach64Accelerator::allocContextBuffer(Mach64GLUserClient *c)
{
    (void)c;
    return ++fStamp;   /* opaque handle; real impl allocates per-engine state */
}

/* ---- submission / fences (stubs until the ring/MMIO path lands) ---- */
UInt32 Mach64Accelerator::rotateCommandBuffer(Mach64GLUserClient *c, UInt32 size)
{
    (void)c; (void)size;
    /* Real impl: submit the filled buffer to BM_ADDR / the DMA ring
     * (byte-reversing to LE), return the next buffer id. */
    return ++fStamp;
}

IOReturn Mach64Accelerator::freeCommandBuffer(Mach64GLUserClient *c, UInt32 id)
{
    (void)c; (void)id;
    return kIOReturnSuccess;
}

IOReturn Mach64Accelerator::waitForStamp(UInt32 stamp)
{
    (void)stamp;
    return kIOReturnSuccess;   /* real impl polls the completion stamp */
}

IOReturn Mach64Accelerator::waitForIdle(void)  { return kIOReturnSuccess; }
UInt32   Mach64Accelerator::engineStatus(void) { return 0; }

IOReturn Mach64Accelerator::setSurface(Mach64GLUserClient *c,
                                       UInt32 a, UInt32 b, UInt32 cc, UInt32 d)
{
    (void)c; (void)a; (void)b; (void)cc; (void)d;
    return kIOReturnSuccess;
}
