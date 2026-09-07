/*
 * atiragepro_abi.h - the Path-B contract between OUR ATIRageProGLDriver.bundle
 * (userspace) and Apple's shipped ATIRagePro.kext.  Distinct from the legacy
 * mach64_abi.h (which described the Rage128 driver set + our own dropped kext).
 *
 * Provenance: docs/re-notes/apple-sharegroup-cmdbuf.md +
 *             docs/re-notes/apple-glcontext-protocol.md
 * RE'd offline from "/Volumes/macUSB/panther ati drivers/ATIRagePro.kext"
 * (Mach-O MH_OBJECT ppc, symbol-rich; __text file off = vaddr + 0x33c).
 *
 * The two-layer architecture: IOKit user clients IOATIRProGLContext /
 * IOATIRProSurface / IOATIRProShared wrap engine objects ATIR3GLContext /
 * ATIR3Surface / ATIRagePro.  Apple never shipped the ATIRageProGLDriver.bundle
 * its IOGLBundleName points at -- we ARE that bundle.
 */
#ifndef ATIRAGEPRO_ABI_H
#define ATIRAGEPRO_ABI_H

#include <stdint.h>

/* ---- IOServiceOpen connection types (HARDWARE-VERIFIED) --------------------
 * type>2 => 0xE00002C2.  HW-verified via the get_state probe (surftype.c) + every
 * live present tool (writetest/fspresent/flushtest open SURF_TYPE=0):
 *   type 0 = IOATIRProSurface, type 1 = IOATIRProGLContext, type 2 = IOATIRPro2DContext.
 * (newUserClient's per-type factory vtable slots are pure-virtual placeholders
 * patched from the IOAccelerator base at load, so they can't name the types
 * statically -- the live tools are ground truth.)
 */
#define ATIRPRO_CONNECT_SURFACE  0   /* IOATIRProSurface    */
#define ATIRPRO_CONNECT_GL       1   /* IOATIRProGLContext  */
#define ATIRPRO_CONNECT_2D       2   /* IOATIRPro2DContext  */

/* ---- IOKit IOExternalMethod dispatch types (the methodDescs `flags` field) ---
 * NB: kIOUCScalarIStructI == 4 (NOT 1).  count0/count1 = scalarIn/scalarOut for
 * ScalarIScalarO; scalarIn/structInSize for ScalarIStructI; etc.  0xffffffff in a
 * count = kIOUCVariableStructureSize.
 */
#define ATIRPRO_UC_ScalarIScalarO  0
#define ATIRPRO_UC_ScalarIStructO  2
#define ATIRPRO_UC_StructIStructO  3
#define ATIRPRO_UC_ScalarIStructI  4

/* ---- IOATIRProGLContext external methods -- FULLY CONFIRMED (2026-09-06) ------
 * Extracted from the kext's IOExternalMethod table (GL methodDescs @ vaddr 0x174c0,
 * entry stride 0x18: {object, IOMethod[2w], flags, count0, count1}), func pointers
 * resolved to symbols.  The table has EXACTLY 15 selectors (0..14); there is NO
 * new_vertex_buffer/delete_vertex_buffer selector on the GL client (that Rage128
 * guess was wrong).  Format:  sel = name  [dispatch type]  in/out counts.
 */
enum {
    kATIRPro_set_surface          = 0,  /* @0x6440 ScalarIStructI in=4 (find_surface_for_id+bind ctx+0x10c) */
    kATIRPro_set_swap_rect        = 1,  /* @0x6700 ScalarIStructI in=4  */
    kATIRPro_set_swap_interval    = 2,  /* @0x6750 ScalarIStructI in=2  */
    kATIRPro_get_config           = 3,  /* @0x6798 ScalarIScalarO out=3 (cold-safe) */
    kATIRPro_get_status           = 4,  /* @0x6938 ScalarIScalarO out=1 */
    kATIRPro_get_surface_size     = 5,  /* @0x6aa8 ScalarIScalarO out=4 */
    kATIRPro_get_surface_info     = 6,  /* @0x6b3c ScalarIScalarO in=1 out=3 */
    kATIRPro_read_buffer          = 7,  /* @0x6c50 StructIStructO (var in) */
    kATIRPro_finish               = 8,  /* @0x7070 ScalarIStructI in=0 (drain+idle) */
    kATIRPro_wait_for_stamp       = 9,  /* @0x70c0 ScalarIStructI in=1 (drain+idle) */
    kATIRPro_new_texture          = 10, /* @0x71e4 StructIStructO (var) -> shared->new_texture */
    kATIRPro_delete_texture       = 11, /* @0x73c8 ScalarIStructI in=1 */
    kATIRPro_become_global_shared = 12, /* @0x7484 ScalarIStructI in=1 (publish/relinquish; optional) */
    kATIRPro_pageoff_texture      = 13, /* @0x758c ScalarIStructI in=2 */
    kATIRPro_scale_surface        = 14, /* @0x710c ScalarIStructI in=3 */
    kATIRPro_GL_SEL_COUNT         = 15,
};

/* ---- IOATIRProSurface external methods -- FULLY CONFIRMED (2026-09-06) --------
 * Surface methodDescs @ vaddr 0x18420 (same 0x18 stride); 16 selectors (0..15).
 * SEPARATE client from the GL table (sel 7 = read_buffer on GL, but set_id_mode
 * on a surface).  This is Apple's standard IOAccelSurface protocol.
 */
enum {
    kATIRProSurf_surface_read_lock_options    = 0,  /* @0xc140 ScalarIStructO in=1 */
    kATIRProSurf_surface_read_unlock_options  = 1,  /* @0xc248 ScalarIScalarO in=1 */
    kATIRProSurf_get_state                    = 2,  /* @0xb440 ScalarIScalarO out=1 */
    kATIRProSurf_surface_write_lock_options   = 3,  /* @0xc1c4 ScalarIStructO in=1 */
    kATIRProSurf_surface_write_unlock_options = 4,  /* @0xc2b8 ScalarIScalarO in=1 */
    kATIRProSurf_surface_read                 = 5,  /* @0xb558 StructIStructO      */
    kATIRProSurf_set_shape_backing            = 6,  /* @0xc62c ScalarIStructI in=4 */
    kATIRProSurf_set_id_mode                  = 7,  /* @0xaf48 ScalarIScalarO in=2 (register CGS id+mode -> accel+0x5c) */
    kATIRProSurf_set_scale                    = 8,  /* @0xcb70 ScalarIStructI in=1 */
    kATIRProSurf_set_shape                    = 9,  /* @0xc5ec ScalarIStructI in=2 + IOAccelDeviceRegion struct */
    kATIRProSurf_surface_flush                = 10, /* @0xb498 ScalarIScalarO in=2 */
    kATIRProSurf_surface_query_lock           = 11, /* @0xc328 ScalarIScalarO      */
    kATIRProSurf_surface_read_lock            = 12, /* @0xc184 ScalarIStructO      */
    kATIRProSurf_surface_read_unlock          = 13, /* @0xc280 ScalarIScalarO      */
    kATIRProSurf_surface_write_lock           = 14, /* @0xc208 ScalarIStructO      */
    kATIRProSurf_surface_write_unlock         = 15, /* @0xc2f0 ScalarIScalarO      */
    kATIRProSurf_SURF_SEL_COUNT               = 16,
};

/* ---- IOConnectMapMemory memory types on the GL context ----------------------
 * (clientMemoryForType jump table @0x573c).  BOTH require ctx+0x10c (the bound
 * surface from set_surface) to be non-NULL first, else the map NULL-derefs.
 */
enum {
    ATIRPRO_MEM_COMMAND = 1,  /* [note] ctx[0xb8] {reg,val} command region        */
    ATIRPRO_MEM_CONTEXT = 2,  /* [note] ctx[0xd0] context buffer, size 0x1000     */
};

/* ---- VendorCommandBufferHeader (0x20 bytes; body = {reg,val} pairs @ +0x20) --
 * Verified vs init_command_buffer_header@0x2bb0/@0x7a64, consumer
 * ATIR3GLContext::submit_context_buffer@0x139c4, drain ATIRagePro::submit_buffer
 * @0x12730.  The CLIENT writes body[] + count + flags bit0, then calls a fence
 * selector (finish/wait_for_stamp); the kext snapshots `count` dwords and PIO-
 * drains them as byte-reversed {reg,val} MMIO writes (reg used RAW, full offset).
 */
typedef struct ATIRProCmdBufHeader {
/* 0x00 */ uint32_t head;      /* opaque; snapshot-copied by kext                 */
/* 0x04 */ uint32_t rsvd04;
/* 0x08 */ uint32_t rsvd08;
/* 0x0c */ uint32_t rsvd0c;
/* 0x10 */ uint32_t capacity;  /* body capacity in DWORDS = (size-0x20)/4 [kext]  */
/* 0x14 */ uint32_t count;     /* # body DWORDS to drain; CLIENT advances this    */
/* 0x18 */ uint32_t stamp;     /* last submit stamp (= ctx[0x7c]) [kext]          */
/* 0x1c */ uint32_t flags;     /* bit0 = VALID/SUBMIT; kext clears after drain    */
/* 0x20 */ uint32_t body[1];   /* {reg,val} u32 pairs; count = pairs*2            */
} ATIRProCmdBufHeader;

#define ATIRPRO_CMDBUF_HDR_BYTES   0x20u
#define ATIRPRO_CMDBUF_FLAG_SUBMIT 0x1u
/* init_command_buffer_header seeds body[0]=0x01000000 (leading marker; the
 * client overwrites count with real pairs*2 before triggering).  Odd DWORD
 * counts are padded with a trailing 0xffffffff. */
#define ATIRPRO_CMDBUF_SEED0       0x01000000u
#define ATIRPRO_CMDBUF_PAD         0xffffffffu

#endif /* ATIRAGEPRO_ABI_H */
