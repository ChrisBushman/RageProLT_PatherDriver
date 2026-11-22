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
#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOFramebuffer.h>

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

    // ---- kernel-side DMA submission state (the proven ring path) --------
    IOMemoryMap              *fRegMap;    // uncached kernel map of BAR0
    volatile void            *fRegBase;   // fRegMap VA + MACH64_REG_WINDOW
    IOMemoryMap              *fRingMap;   // uncached kernel map of the ring
    volatile void            *fRingKVA;   // fRingMap VA (write descriptors here)
    IOLock                   *fLock;      // serialize submits from our clients
    volatile UInt32           fCompletedStamp; // highest retired stamp
    UInt32                    fCmdBufSize;     // bytes in the command buffer
    UInt32                    fFlipPending;    // 1 => a page flip was written, not yet retired
    UInt32                    fFlipScanline;   // scanline at the flip write (vblank-load detect)
    UInt32                    fOrigCrtc;       // desktop CRTC_OFF_PITCH, saved at first flip, restored in stop()

    // ---- display mode changes (mirrors ATIRagePro::display_mode_will/did_change, docs/re-notes/display-mode-change.md) ----
    enum { kMaxFB = 4 };
    IONotifier               *fFBPublish;      // publish notification for IOFramebuffers on our PCI device
    IOFramebuffer            *fFBs[kMaxFB];    // framebuffers we registered with (not retained: they outlive us on this device)
    IONotifier               *fFBNotify[kMaxFB];
    int                       fNumFB;
    volatile SInt32           fModeDepth;      // nested will/did count across heads (Apple's this+0xc4)
    volatile UInt32           fModeGen;        // odd while a mode change is in progress; bumped on each edge
    bool                      fRedirected;     // user mappings of fFB are redirected

    // ---- register / ring MMIO helpers (little-endian, see mach64_regs.h) --
    UInt32 regRead (UInt32 off) const;
    void   regWrite(UInt32 off, UInt32 v) const;
    void   ringWrite(UInt32 off, UInt32 v) const;
    bool   waitFifo(int entries) const;   // >= `entries` free FIFO slots
    bool   waitIdle(void) const;          // GUI engine idle
    IOReturn submitCommandBuffer(UInt32 byteCount);  // the quiesce+kick sequence

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
    UInt32 crtcOffPitch(void);   // live display scanout base+pitch register
    UInt32 crtcVTotalDisp(void); // CRTC vertical total/display: V_DISP[26:16]=lines-1
    UInt32 allocContextBuffer(Mach64GLUserClient *c);

    // ---- submission / fences ----
    UInt32   rotateCommandBuffer(Mach64GLUserClient *c, UInt32 size);
    IOReturn freeCommandBuffer(Mach64GLUserClient *c, UInt32 id);
    IOReturn waitForStamp(UInt32 stamp);
    IOReturn waitForIdle(void);
    UInt32   engineStatus(void);
    IOReturn setSurface(Mach64GLUserClient *c,
                        UInt32 a, UInt32 b, UInt32 cc, UInt32 d);
    IOReturn waitFlip(void);   // block until a pending page flip has retired (vblank loaded)

    // ---- display mode changes ----
    UInt32   modeGen(void) const { return fModeGen; }
    bool     modeChanging(void) const { return (fModeGen & 1u) != 0; }
    bool     fbPublished(IOService *svc);                       // gIOPublishNotification for IOFramebuffer
    IOReturn fbEvent(IOFramebuffer *fb, IOIndex event, void *info);   // IOFramebuffer notification
};

#endif /* MACH64_ACCELERATOR_H */
