/*
 * RageStubFB - a minimal IOFramebuffer for the headless Rage LT Pro (PCI 1002:4c49).
 *
 * Apple's ATIRagePro accelerator only brings up its user clients (surface/GL/2D) once it has found an IOFramebuffer
 * among the children of its PCI device (IOATIRProAccelerator::findFramebuffers/foundFramebuffer).  OpenFirmware never
 * builds a display driver for an un-POSTed card, so there is none.  This stub is that framebuffer: one 640x480x15bpp (5-5-5) mode
 * on the 8 MB VRAM aperture, NO connections.  The mode itself is set by the card's option ROM at boot (boot/RageProInit,
 * docs/re-notes/rage-bios-vbe-modeset.md); the stub verifies it is live and un-blanks the CRTC (CRTC_GEN_CNTL.CRTC_DISPLAY_DIS).  Load it BEFORE ATIRagePro.kext.  Diagnostic/bring-up only.
 */
#ifndef RAGESTUBFB_H
#define RAGESTUBFB_H
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/pci/IOPCIDevice.h>

class RageStubFB : public IOFramebuffer
{
    OSDeclareDefaultStructors(RageStubFB)
    IOPCIDevice *fPCI;
    IOMemoryMap *fRegMap;        /* last 2 KB of BAR0: the chip's register block */
    volatile UInt8 *fRegs;
    bool mapRegisters(void);
    bool replayInit(void);
    void clearScanout(void);
    IODeviceMemory *viewRange(UInt32 length);
    UInt32 rd(UInt32 reg);
    void wr(UInt32 reg, UInt32 val);
    void checkDisplay(bool unblank);
    void probeAccelerator(void);
    void notifyModeChange(void);
    /* DDC2B (I2C) over GP_IO: reads the monitor's EDID */
    void ddcSet(int clk, int dat);
    int  ddcClk(void);
    int  ddcDat(void);
    bool ddcStart(void);
    void ddcStop(void);
    bool ddcPutByte(UInt8 b);
    UInt8 ddcGetByte(bool ack);
    bool readEdid(UInt8 *buf, int blockNo);
    void probeEdid(void);
    void ddcDiag(void);
public:
    virtual IOReturn setProperties(OSObject *props);
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual IOReturn enableController(void);
    virtual IODeviceMemory *getApertureRange(IOPixelAperture aperture);
    virtual IODeviceMemory *getVRAMRange(void);
    virtual const char *getPixelFormats(void);
    virtual IOItemCount getDisplayModeCount(void);
    virtual IOReturn getDisplayModes(IODisplayModeID *all);
    virtual IOReturn getInformationForDisplayMode(IODisplayModeID mode, IODisplayModeInformation *info);
    virtual UInt64 getPixelFormatsForDisplayMode(IODisplayModeID mode, IOIndex depth);
    virtual IOReturn getPixelInformation(IODisplayModeID mode, IOIndex depth, IOPixelAperture ap, IOPixelInformation *pi);
    virtual IOReturn getCurrentDisplayMode(IODisplayModeID *mode, IOIndex *depth);
    virtual IOReturn setDisplayMode(IODisplayModeID mode, IOIndex depth);
    virtual IOReturn getStartupDisplayMode(IODisplayModeID *mode, IOIndex *depth);
    virtual IOItemCount getConnectionCount(void);
    virtual IOReturn getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, UInt32 *value);
    virtual IOReturn setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, UInt32 value);
    virtual IOReturn getTimingInfoForDisplayMode(IODisplayModeID mode, IOTimingInformation *info);
    virtual IOReturn setCLUTWithEntries(IOColorEntry *colors, UInt32 index, UInt32 numEntries, IOOptionBits options);
    virtual IOReturn setAttribute(IOSelect attribute, UInt32 value);
    virtual IOReturn getAttribute(IOSelect attribute, UInt32 *value);
    virtual IOReturn setStartupDisplayMode(IODisplayModeID mode, IOIndex depth);
    virtual IOReturn setGammaTable(UInt32 channelCount, UInt32 dataCount, UInt32 dataWidth, void *data);
};
#endif
