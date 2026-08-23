/*
 * Mach64GLUserClient.h - IOUserClient for the Rage LT Pro GL context.
 *
 * Modeled on IOATIR128GLContext (reversed from ATIRage128.kext).  This is
 * the kernel half of the contract the GLDriver plugin opens: 17 io_connect
 * selectors + clientMemoryForType for regions {0,1,2,4}.
 *
 * Requires the Mac OS X 10.3 IOKit C++ headers to build.
 */
#ifndef MACH64_GL_USER_CLIENT_H
#define MACH64_GL_USER_CLIENT_H

#include <IOKit/IOUserClient.h>
#include "../common/mach64_abi.h"

class Mach64Accelerator;   // the IOService that owns the register aperture

class Mach64GLUserClient : public IOUserClient
{
    OSDeclareDefaultStructors(Mach64GLUserClient)

    Mach64Accelerator     *fProvider;
    task_t                 fTask;
    IOExternalMethod       fMethods[kMach64Sel_COUNT];

public:
    // IOUserClient overrides
    virtual bool     initWithTask(task_t owningTask, void *sid, UInt32 type);
    virtual bool     start(IOService *provider);
    virtual IOReturn clientClose(void);
    virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *opts,
                                         IOMemoryDescriptor **mem);
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
                                                         UInt32 index);

    // ---- selector handlers (see mach64_abi.h) ----
    IOReturn get_config(UInt32 *c0, UInt32 *ctxTok, UInt32 *c1);        // sel 3
    IOReturn get_status(UInt32 *status);                               // sel 4
    IOReturn set_surface(UInt32 surfaceID, UInt32 modeBits,
                         UInt32 a, UInt32 b);                          // sel 0
    IOReturn new_vertex_buffer(UInt32 size, UInt32 *bufferID);         // sel 15
    IOReturn delete_vertex_buffer(UInt32 bufferID);                    // sel 16
    IOReturn finish(void);                                             // sel 8
    IOReturn wait_for_stamp(UInt32 stamp);                            // sel 9
    // ... new_texture / delete_texture / read_buffer / swap etc. ...
};

#endif /* MACH64_GL_USER_CLIENT_H */
