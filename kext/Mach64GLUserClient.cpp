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
#include <IOKit/IORegistryEntry.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include <kern/lock.h>      /* mutex_lock / mutex_unlock (Apple's accelerator mutex is a Mach mutex_t) */

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
    fAccel = 0; fMutex = 0; fHeld = 0; fCall = 0;

    /* The 17-selector table (indices/counts verified from the Rage128 kext). */
    IOExternalMethod tbl[kMach64Sel_COUNT] = {
        M(set_surface,        kIOUCScalarIScalarO, 1, 0),               // 0: page-flip (CRTC_OFF_PITCH)
        M(wait_flip,          kIOUCScalarIScalarO, 0, 0),               // 1: deferred flip wait
        /* set_swap_interval*/M(set_surface,       kIOUCScalarIStructI, 2, 0), // 2
        M(get_config,         kIOUCScalarIScalarO, 0, 3),               // 3
        M(get_status,         kIOUCScalarIScalarO, 0, 1),               // 4
        M(get_surface_size,   kIOUCScalarIScalarO, 0, 4),               // 5
        /* get_surface_info */M(get_status,        kIOUCScalarIScalarO, 1, 3), // 6
        /* read_buffer */     M(finish,            kIOUCStructIStructO, 0xffffffff, 0), // 7: GLEngine never calls it for glReadPixels (see docs/re-notes/readpixels.md)
        M(finish,             kIOUCScalarIStructI, 0, 0),               // 8
        M(wait_for_stamp,     kIOUCScalarIStructI, 1, 0),               // 9
        /* new_texture */     M(finish,            kIOUCStructIStructO, 0xffffffff, 0xffffffff), //10
        /* delete_texture */  M(finish,            kIOUCScalarIStructI, 1, 0), //11
        /* become_global_sh */M(finish,            kIOUCScalarIStructI, 1, 0), //12
        /* pageoff_texture */ M(finish,            kIOUCScalarIStructI, 2, 0), //13
        /* scale_surface */   M(finish,            kIOUCScalarIStructI, 3, 0), //14
        M(new_vertex_buffer,  kIOUCScalarIScalarO, 1, 1),               //15
        M(delete_vertex_buffer,kIOUCScalarIStructI,1, 0),               //16
        M(accel_hold,         kIOUCScalarIScalarO, 1, 0),               //17: take Apple's accelerator mutex and keep it
        M(accel_release,      kIOUCScalarIScalarO, 0, 0),               //18: release it
        M(get_mode_gen,       kIOUCScalarIScalarO, 0, 1),               //19: display-mode generation
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
    *hi    = ((fProvider->crtcVTotalDisp() >> 16) & 0x7ff) + 1;  // displayed lines (height)
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
/* ---- Apple's accelerator mutex, held across calls (single-thread kick/poll mode; see docs/re-notes/single-thread-engine.md) ----
 * Apple's kext serialises every engine user (GL drains, WindowServer 2D drains, our patched selectors) with the Mach mutex at accel[0x544].  A GUI-bus-master batch must
 * not overlap another client's register writes, so the driver takes it with accel_hold before kicking a batch and gives it back with accel_release once the batch has drained.
 * This is the same thread's lock/unlock across two calls.  A DIFFERENT thread must not unlock while another thread waits (mutex_unlock_wakeup panics unless the unlocking
 * activation owns the lock): the RELEASE kernel keeps the owning activation in the lock word's upper 30 bits (flag bits: 1 = interlock, 2 = waiters), so clientClose
 * (a dying client's thread) first makes itself the owner, then unlocks normally. */
static void m64_adopt_and_unlock(void *m)
{
    UInt32 me, w, nw, *lw = (UInt32 *)m;
    __asm__ volatile("mfsprg %0,1" : "=r"(me));                      /* the current activation */
    do {
        __asm__ volatile("lwarx %0,0,%1" : "=r"(w) : "r"(lw) : "memory");
        if (w & 1) continue;                                          /* interlock busy: retry */
        nw = (me & ~3u) | (w & 2u);
        __asm__ volatile("stwcx. %2,0,%1\n\tmfcr %0" : "=r"(w) : "r"(lw), "r"(nw) : "cr0", "memory");
        if (w & 0x20000000u) break;                                   /* cr0.eq set: store succeeded */
    } while (1);
    mutex_unlock((mutex_t *)m);
}

/* IOATIRProAccelerator is not in the registry (ATIRagePro owns it privately), so the driver reads its mutex pointer from Apple's own GL context (ctx[0xb0] -> accel[0x544], through
 * Apple's patched peek selector) and passes the kernel address in.  We only accept the address that was first given (a different one would mean a different accelerator). */
/* WATCHDOG.  A client that dies (or stalls) while holding the mutex would wedge the machine: Apple's own teardown of that client's GL context needs the same mutex, so the dying
 * process never finishes exiting and our clientClose never runs (observed: kill -9 of a holder = process stuck in 'E', every other 2D/GL user blocked).  So every hold arms a
 * timer; if it is still held when it fires, a kernel thread releases it (adopting the lock word first, so a waiting WindowServer is woken correctly). */
#define M64_HOLD_LIMIT_MS 2000
static void m64_wd_tramp(thread_call_param_t p0, thread_call_param_t p1) { ((Mach64GLUserClient *)p0)->watchdog(); }
void Mach64GLUserClient::watchdog(void)
{
    if (OSCompareAndSwap(1, 0, &fHeld)) {
        IOLog("Mach64GLUserClient %p: accelerator mutex held for more than %d ms -- releasing it from the watchdog\n", this, M64_HOLD_LIMIT_MS);
        m64_adopt_and_unlock(fMutex);
    }
    release();                                   /* the reference taken when the timer was armed */
}
static UInt32 sApplMutex = 0;
IOReturn Mach64GLUserClient::get_mode_gen(UInt32 *gen) { *gen = fProvider->modeGen(); return kIOReturnSuccess; }

IOReturn Mach64GLUserClient::accel_hold(UInt32 mutexAddr)
{
    if (fHeld) return kIOReturnBusy;
    if (fProvider->modeChanging()) return kIOReturnNotReady;   /* Apple is stopping/re-initialising the engine: no user kicks until "did" */
    if (mutexAddr < 0x10000u || mutexAddr > 0xdfffffffu || (mutexAddr & 3u)) return kIOReturnBadArgument;
    if (sApplMutex && sApplMutex != mutexAddr) return kIOReturnBadArgument;
    sApplMutex = mutexAddr;
    fMutex = (void *)mutexAddr;
    if (!fCall) { fCall = thread_call_allocate(m64_wd_tramp, (thread_call_param_t)this); if (!fCall) return kIOReturnNoMemory; }
    mutex_lock((mutex_t *)fMutex);
    fHeld = 1;
    {   AbsoluteTime dl; clock_interval_to_deadline(M64_HOLD_LIMIT_MS, 1000u * 1000u, &dl);
        retain();                                /* the callback releases this; accel_release/free drop it again when the cancel wins */
        if (thread_call_enter_delayed((thread_call_t)fCall, dl)) release(); }   /* already pending (should not be): keep one reference */
    return kIOReturnSuccess;
}

IOReturn Mach64GLUserClient::accel_release(void)
{
    if (!OSCompareAndSwap(1, 0, &fHeld)) return kIOReturnNotReady;     /* not held, or the watchdog already took it away */
    if (fCall && thread_call_cancel((thread_call_t)fCall)) release();  /* the callback will not run: drop its reference */
    mutex_unlock((mutex_t *)fMutex);
    return kIOReturnSuccess;
}

void Mach64GLUserClient::free(void)
{
    if (OSCompareAndSwap(1, 0, &fHeld)) { IOLog("Mach64GLUserClient: client went away holding the accelerator mutex; releasing it\n"); m64_adopt_and_unlock(fMutex); }
    if (fCall) { thread_call_cancel((thread_call_t)fCall); thread_call_free((thread_call_t)fCall); fCall = 0; }
    super::free();
}

IOReturn Mach64GLUserClient::clientClose(void)
{
    if (OSCompareAndSwap(1, 0, &fHeld)) { IOLog("Mach64GLUserClient: client closed holding the accelerator mutex; releasing it\n"); m64_adopt_and_unlock(fMutex); }
    terminate(); return kIOReturnSuccess;
}
bool Mach64GLUserClient::initWithTask(task_t t, void *sid, UInt32 type)
{
    if (type != MACH64_GL_CONNECT_TYPE) return false;   // only GL type (1)
    if (!super::initWithTask(t, sid, type)) return false;
    fTask = t; return true;
}
