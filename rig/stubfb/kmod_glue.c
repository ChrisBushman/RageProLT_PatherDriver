/*
 * kmod_glue.c - module info + entry points for the RageStubFB kext.
 *
 * Standard IOKit-kext glue: _start/_stop come from libkmodc++.a (they run the
 * C++ static constructors that register the OSMetaClass objects, then call
 * _realmain/_antimain, which are 0 for a pure-IOKit kext with no custom
 * module start/stop).
 */
#include <mach/mach_types.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

KMOD_EXPLICIT_DECL(com.local.RageStubFB, "0.1", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t  *_antimain = 0;
