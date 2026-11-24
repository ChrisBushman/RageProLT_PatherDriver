/* common/rage_types.h - fixed-width integer types for code shared by the kext and host tests.  The 10.3 Kernel framework ships its own
 * <stdint.h> (uint32_t = unsigned int); note that the kernel's UInt32 is `unsigned long`, a DIFFERENT type, so kext interfaces that
 * take shared-code pointers use uint32_t, not UInt32. */
#ifndef RAGE_TYPES_H
#define RAGE_TYPES_H
#include <stdint.h>
#endif
