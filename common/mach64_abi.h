/*
 * mach64_abi.h - the plugin <-> kext contract, reverse-engineered from the
 * Panther ATIRage128 driver set (ATIRage128GLDriver.bundle + ATIRage128.kext).
 * Shared by the GLDriver bundle (userspace) and the kext.
 *
 * Provenance: docs/re-notes/ABI.md
 */
#ifndef MACH64_ABI_H
#define MACH64_ABI_H

/* Kernel-safe integer types: the kext builds with -nostdinc (no <stdint.h>),
 * so bridge to libkern's UInt* there; userspace uses C99 <stdint.h>. */
#if defined(KERNEL)
#include <libkern/OSTypes.h>
typedef UInt32 m64_u32;
typedef UInt16 m64_u16;
typedef UInt8  m64_u8;
#else
#include <stdint.h>
typedef uint32_t m64_u32;
typedef uint16_t m64_u16;
typedef uint8_t  m64_u8;
#endif

/* ---- IOServiceOpen connection type ----------------------------------
 * gldCreateContext opens the user client with type=1 (the GL context).
 */
#define MACH64_GL_CONNECT_TYPE   1

/* ---- IOConnectMapMemory memory types (clientMemoryForType) -----------
 * Verified from IOATIR128GLContext::clientMemoryForType jump table.
 * (turn 14: type 1 = registers was WRONG; corrected here from the kext.)
 */
enum {
    MACH64_MEM_REGISTERS   = 0,  /* MMIO register aperture (fixed BAR)      */
    MACH64_MEM_SURFACE     = 1,  /* VRAM color/render surface (allocated)   */
    MACH64_MEM_CONTROL     = 2,  /* 4KB shared status/control page          */
    MACH64_MEM_FB_APERTURE = 3,  /* second fixed aperture (not used by GL)  */
    MACH64_MEM_CMDBUF      = 4,  /* growable kernel/user-shared command buf */
    MACH64_MEM_RING        = 5,  /* DMA descriptor ring (phys-contig, uncached)*/
};

/* In THIS build, get_config (selector 3) returns DMA bus addresses so the
 * userspace DMA orchestrator can build descriptors:
 *   config0 = command-buffer bus address
 *   ctx_token = descriptor-ring bus address
 *   config1 = chip config word */

/* ---- io_connect_method selector indices ------------------------------
 * The complete 17-entry IOExternalMethod table recovered from
 * ATIR128GLContext::getTargetAndMethodForIndex.  in/out are scalar counts
 * (or 'S' = variable-size struct).
 */
enum {
    kMach64Sel_set_surface        = 0,  /* page-flip write: ScalarI_ScalarO in=1  */
    kMach64Sel_wait_flip          = 1,  /* deferred flip wait: ScalarI_ScalarO in=0 */
    kMach64Sel_set_swap_interval  = 2,  /* ScalarI_StructI  in=2          */
    kMach64Sel_get_config         = 3,  /* ScalarI_ScalarO  in=0 out=3    */
    kMach64Sel_get_status         = 4,  /* ScalarI_ScalarO  in=0 out=1    */
    kMach64Sel_get_surface_size   = 5,  /* ScalarI_ScalarO  in=0 out=4    */
    kMach64Sel_get_surface_info   = 6,  /* ScalarI_ScalarO  in=1 out=3    */
    kMach64Sel_read_buffer        = 7,  /* StructI_StructO                */
    kMach64Sel_finish             = 8,  /* ScalarI_StructI  in=0          */
    kMach64Sel_wait_for_stamp     = 9,  /* ScalarI_StructI  in=1          */
    kMach64Sel_new_texture        = 10, /* StructI_StructO                */
    kMach64Sel_delete_texture     = 11, /* ScalarI_StructI  in=1          */
    kMach64Sel_become_global_shared = 12,
    kMach64Sel_pageoff_texture    = 13, /* ScalarI_StructI  in=2          */
    kMach64Sel_scale_surface      = 14, /* ScalarI_StructI  in=3          */
    kMach64Sel_new_vertex_buffer  = 15, /* ScalarI_ScalarO  in=1 out=1    */
    kMach64Sel_delete_vertex_buffer = 16, /* ScalarI_StructI in=1         */
    kMach64Sel_accel_hold         = 17, /* ScalarI_ScalarO in=1 out=0: take Apple's accelerator mutex (kernel address passed in: accel[0x544], read through Apple's patched peek selector) and KEEP it across the return */
    kMach64Sel_accel_release      = 18, /* ScalarI_ScalarO in=0 out=0: release it (same thread; the dying-client path releases it safely too) */
    kMach64Sel_get_mode_gen       = 19, /* ScalarI_ScalarO in=0 out=1: display-mode generation (odd = a mode change is in progress; any change = every cached VRAM offset is stale) */
    kMach64Sel_COUNT              = 20,
};

/* get_config (selector 3) returns 3 dwords into the plugin's context. */
typedef struct {
    m64_u32 config0;   /* hw config word ([hw+0x8c]); plugin tests bit 1 */
    m64_u32 ctx_token; /* allocated context-buffer handle                */
    m64_u32 config1;   /* second config word ([hw+0x90])                 */
} mach64_get_config_out;

/* Shared command-buffer submission model (turns 9 & 15):
 * the plugin writes a verified register-write stream into the type-4 buffer;
 * new_vertex_buffer() hands back the next buffer AND kicks the previous;
 * wait_for_stamp() fences on completion.  Header prepended to each buffer.
 */
typedef struct {
    uint32_t stamp;     /* fence value the kernel signals on completion    */
    uint32_t dword_count;
    uint32_t reserved[2];
    /* uint32_t stream[]  -- (ADRINDEX(reg), value) pairs follow           */
} mach64_cmdbuf_header;

#endif /* MACH64_ABI_H */
