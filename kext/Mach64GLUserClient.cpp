/*
 * Mach64GLUserClient.cpp - kernel half of the GL contract.
 * Builds against the Mac OS X 10.3 IOKit C++ headers (not host-buildable).
 *
 * The selector table and clientMemoryForType switch are transcribed from
 * the reversed IOATIR128GLContext (see docs/re-notes/ABI.md).
 */
#include "Mach64GLUserClient.h"
#include "Mach64Accelerator.h"
#include <IOKit/IOLib.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(Mach64GLUserClient, IOUserClient)

/* One IOExternalMethod initializer.  object=0 => target is `this`. */
#define M(fn, flags, in, out) \
    { 0, (IOMethod)&Mach64GLUserClient::fn, (IOOptionBits)(flags), (in), (out) }

bool Mach64GLUserClient::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    fProvider = OSDynamicCast(Mach64Accelerator, provider);
    if (!fProvider) return false;

    /* The 17-selector table (indices/counts verified from the Rage128 kext). */
    IOExternalMethod tbl[kMach64Sel_COUNT] = {
        M(set_surface,        kIOUCScalarIScalarO, 1, 0),               // 0: page-flip (CRTC_OFF_PITCH)
        M(wait_flip,          kIOUCScalarIScalarO, 0, 0),               // 1: deferred flip wait
        /* set_swap_interval*/M(set_surface,       kIOUCScalarIStructI, 2, 0), // 2
        M(get_config,         kIOUCScalarIScalarO, 0, 3),               // 3
        M(get_status,         kIOUCScalarIScalarO, 0, 1),               // 4
        M(get_surface_size,   kIOUCScalarIScalarO, 0, 4),               // 5
        /* get_surface_info */M(get_status,        kIOUCScalarIScalarO, 1, 3), // 6
        /* read_buffer */     M(finish,            kIOUCStructIStructO, 0xffffffff, 0), // 7
        M(finish,             kIOUCScalarIStructI, 0, 0),               // 8
        M(wait_for_stamp,     kIOUCScalarIStructI, 1, 0),               // 9
        /* new_texture */     M(finish,            kIOUCStructIStructO, 0xffffffff, 0xffffffff), //10
        /* delete_texture */  M(finish,            kIOUCScalarIStructI, 1, 0), //11
        /* become_global_sh */M(finish,            kIOUCScalarIStructI, 1, 0), //12
        /* pageoff_texture */ M(finish,            kIOUCScalarIStructI, 2, 0), //13
        /* scale_surface */   M(finish,            kIOUCScalarIStructI, 3, 0), //14
        M(new_vertex_buffer,  kIOUCScalarIScalarO, 1, 1),               //15
        M(delete_vertex_buffer,kIOUCScalarIStructI,1, 0),               //16
    };
    for (int i = 0; i < kMach64Sel_COUNT; i++) fMethods[i] = tbl[i];
    return true;
}

IOExternalMethod *
Mach64GLUserClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    if (index >= kMach64Sel_COUNT) return 0;
    *target = this;
    return &fMethods[index];
}

/*
 * clientMemoryForType - hand the plugin its four regions.
 * Types & roles verified from IOATIR128GLContext::clientMemoryForType.
 */
IOReturn
Mach64GLUserClient::clientMemoryForType(UInt32 type, IOOptionBits *opts,
                                        IOMemoryDescriptor **mem)
{
    *opts = kIOMapInhibitCache;   // default for all: never leave *opts garbage,
                                  // and device memory must be uncached on PPC
    switch (type) {
    case MACH64_MEM_REGISTERS:                 // 0 : MMIO register aperture
        *mem  = fProvider->registerAperture(); // IODeviceMemory (fixed BAR)
        break;
    case MACH64_MEM_SURFACE:                   // 1 : VRAM render surface
        *mem  = fProvider->allocSurface(this);
        break;
    case MACH64_MEM_CONTROL:                   // 2 : 4KB shared control page
        *mem  = fProvider->controlPage();
        break;
    case MACH64_MEM_CMDBUF:                     // 4 : DMA command buffer
        *mem  = fProvider->commandBuffer(this);
        break;
    case MACH64_MEM_RING:                      // 5 : DMA descriptor ring
        *mem  = fProvider->ringMemory();
        break;
    default:
        return kIOReturnBadArgument;
    }
    /* IOKit RELEASES the returned descriptor after mapping, so hand it back
     * with an extra retain -- otherwise our cached fCmdBuf/fRing get
     * over-released and freed out from under us -> panic on unload. */
    if (*mem) { (*mem)->retain(); return kIOReturnSuccess; }
    return kIOReturnNoMemory;
}

/* selector 3: in this build, returns DMA bus addresses for the userspace DMA
 * orchestrator: c0=cmdbuf bus, ctxTok=ring bus, c1=chip config. */
IOReturn Mach64GLUserClient::get_config(UInt32 *c0, UInt32 *ctxTok, UInt32 *c1)
{
    *c0     = fProvider->cmdBufBus();
    *ctxTok = fProvider->ringBus();
    *c1     = fProvider->chipConfig1();
    return kIOReturnSuccess;
}

/* selector 5: return the LIVE display framebuffer for full-screen render target.
 * CRTC_OFF_PITCH encodes it exactly like DST_OFF_PITCH: bits[31:22]=pitch/8 (px),
 * bits[21:0]=offset in 8-byte units.  We return the raw reg (crtc) plus the
 * decoded byte offset + pixel pitch so the plugin can bind the framebuffer. */
IOReturn Mach64GLUserClient::get_surface_size(UInt32 *crtc, UInt32 *base,
                                              UInt32 *pitch, UInt32 *hi)
{
    UInt32 v = fProvider->crtcOffPitch();
    *crtc  = v;
    *base  = (v & 0x3fffff) << 3;          // display start, byte offset
    *pitch = ((v >> 22) & 0x3ff) * 8;      // pitch in pixels
    *hi    = 0;
    return kIOReturnSuccess;
}

/* selector 15: allocate a command/vertex buffer, kick the previous (turn 15). */
IOReturn Mach64GLUserClient::new_vertex_buffer(UInt32 size, UInt32 *bufferID)
{
    /* fProvider submits the previously-filled buffer to BM_ADDR / the ring
     * (byte-reversing to LE), then returns the next free buffer id. */
    *bufferID = fProvider->rotateCommandBuffer(this, size);
    return kIOReturnSuccess;
}

IOReturn Mach64GLUserClient::wait_for_stamp(UInt32 stamp)
{
    return fProvider->waitForStamp(stamp);   // fence on ring completion
}

/* remaining handlers (set_surface/finish/get_status/...) elided in skeleton */
IOReturn Mach64GLUserClient::finish(void)                     { return fProvider->waitForIdle(); }
IOReturn Mach64GLUserClient::get_status(UInt32 *s)            { *s = fProvider->engineStatus(); return kIOReturnSuccess; }
IOReturn Mach64GLUserClient::set_surface(UInt32 a,UInt32 b,UInt32 c,UInt32 d){ return fProvider->setSurface(this,a,b,c,d); }
IOReturn Mach64GLUserClient::wait_flip(void){ return fProvider->waitFlip(); }
IOReturn Mach64GLUserClient::delete_vertex_buffer(UInt32 id)  { return fProvider->freeCommandBuffer(this,id); }
IOReturn Mach64GLUserClient::clientClose(void)               { terminate(); return kIOReturnSuccess; }
bool Mach64GLUserClient::initWithTask(task_t t, void *sid, UInt32 type)
{
    if (type != MACH64_GL_CONNECT_TYPE) return false;   // only GL type (1)
    if (!super::initWithTask(t, sid, type)) return false;
    fTask = t; return true;
}
