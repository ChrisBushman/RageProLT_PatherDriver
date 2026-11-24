/* common/rage_abi.h - the user-space <-> kext contract of the NEW driver (kernel and tools include the same file).
 * User-client types and selectors are numbered here and nowhere else. */
#ifndef RAGE_ABI_H
#define RAGE_ABI_H

#include "rage_log.h"

#define RAGE_CLIENT_LOG   0x4c4f4700u            /* 'LOG': diagnostics client (Phase 2) */

enum { RAGE_LOG_SEL_INFO = 0, RAGE_LOG_SEL_READ = 1, RAGE_LOG_SEL_MARK = 2, RAGE_LOG_SEL_COUNT = 3 };

/* SEL_INFO: scalarI 0, scalarO 4 = { head, capacity, refused batches, feature bits } */
/* SEL_READ: scalarI 1 = from_seq, structO RAGE_LOG_READ_BYTES = struct rage_log_read_out */
/* SEL_MARK: scalarI 2 = { a0, a1 }: logs a RAGE_EV_MARK so test phases can be lined up with the driver's events */

#define RAGE_LOG_READ_EVENTS 31u
struct rage_log_read_out {                       /* exactly 1024 bytes */
    uint32_t count;                              /* events returned */
    uint32_t lost;                               /* events overwritten before they could be read */
    uint32_t head;                               /* ring head at the time of the read */
    uint32_t capacity;
    uint32_t pad[4];
    struct rage_log_ev ev[RAGE_LOG_READ_EVENTS];
};
#define RAGE_LOG_READ_BYTES ((uint32_t)sizeof(struct rage_log_read_out))

/* feature bits reported by SEL_INFO and published as the kext property "RageLTFeatures" */
#define RAGE_FBIT_PANIC_LOG 0x01u
#define RAGE_FBIT_2D_GA     0x02u
#define RAGE_FBIT_SLEEPWAKE 0x04u
#define RAGE_FBIT_DMA       0x08u
#define RAGE_FBIT_OVERLAY   0x10u
#define RAGE_FBIT_SCALER    0x20u
#define RAGE_FBIT_DVD       0x40u
#define RAGE_FBIT_FS_SCALE  0x80u

#endif
