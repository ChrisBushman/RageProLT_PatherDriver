/*
 * Mach64Accelerator.h - IOService provider for the Rage LT Pro (mach64LB).
 *
 * Matches the ATI PCI device (0x1002:0x4c49, see Info.plist), maps the
 * register + framebuffer apertures, and vends the memory regions / helpers
 * that Mach64GLUserClient hands to the GLDriver plugin.
 *
 * Compile milestone: bodies are minimal but reference real IOKit types so
 * the kext builds against the 10.3 Kernel framework.  Hardware bring-up
 * (actual register programming) comes after this compiles + loads.
 */
#ifndef MACH64_ACCELERATOR_H
#define MACH64_ACCELERATOR_H

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class Mach64GLUserClient;

class Mach64Accelerator : public IOService
{
    OSDeclareDefaultStructors(Mach64Accelerator)

    IOPCIDevice              *fPCI;
    IODeviceMemory           *fRegs;      // MMIO register aperture (BAR)
    IODeviceMemory           *fFB;        // framebuffer / VRAM aperture (BAR)
    IOBufferMemoryDescriptor *fControl;   // 4KB shared control page
    IOBufferMemoryDescriptor *fCmdBuf;    // phys-contig shared command buffer
    IOBufferMemoryDescriptor *fRing;      // phys-contig DMA descriptor ring
    IOPhysicalAddress         fCmdBufBus; // command-buffer bus address
    IOPhysicalAddress         fRingBus;   // ring bus address
    UInt32                    fStamp;     // fence counter
    UInt32                    fConfig0;   // chip config words (get_config)
    UInt32                    fConfig1;

public:
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free(void);

    // ---- regions handed to the user client (see clientMemoryForType) ----
    IODeviceMemory     *registerAperture(void)   { return fRegs; }     // type 0
    IOMemoryDescriptor *allocSurface(Mach64GLUserClient *c);           // type 1
    IOMemoryDescriptor *controlPage(void)        { return fControl; }  // type 2
    IOMemoryDescriptor *commandBuffer(Mach64GLUserClient *c);          // type 4
    IOMemoryDescriptor *ringMemory(void)    { return fRing; }          // type 5

    // ---- DMA bus addresses (returned via get_config in this build) ----
    UInt32 cmdBufBus(void) { return (UInt32)fCmdBufBus; }
    UInt32 ringBus(void)   { return (UInt32)fRingBus; }

    // ---- selector-3 (get_config) data ----
    UInt32 chipConfig0(void) { return fConfig0; }
    UInt32 chipConfig1(void) { return fConfig1; }
    UInt32 allocContextBuffer(Mach64GLUserClient *c);

    // ---- submission / fences ----
    UInt32   rotateCommandBuffer(Mach64GLUserClient *c, UInt32 size);
    IOReturn freeCommandBuffer(Mach64GLUserClient *c, UInt32 id);
    IOReturn waitForStamp(UInt32 stamp);
    IOReturn waitForIdle(void);
    UInt32   engineStatus(void);
    IOReturn setSurface(Mach64GLUserClient *c,
                        UInt32 a, UInt32 b, UInt32 cc, UInt32 d);
};

#endif /* MACH64_ACCELERATOR_H */
