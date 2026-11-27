/* gld_renderer_info: layout read by CGL / CoreGraphics (quartz-acceleration-study.md 1.6). Standalone so a host test can include it. */
#ifndef GLD_RENDERER_INFO_H
#define GLD_RENDERER_INFO_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t handle;        /* 0x00 next-pointer slot / handle (GLEngine fills the list link)        */
    uint32_t renderer_id;   /* 0x04 renderer id (GLEngine ORs 0x20000); Apple: Rage128 0x1000, Radeon 0x1200 */
    uint32_t flags;         /* 0x08 capability flags (Window/FullScreen/MPSafe/Accelerated/Compliant...);
                             * Apple Rage128 0x2513.  Bit 0x1000 is what CoreGraphics' Quartz Extreme check tests (not set: QE stays off) */
    uint32_t buffer_modes;  /* 0x0c                                       */
    uint32_t caps_accel;    /* 0x10 accelerated + buffer-mode bits        */
    uint32_t caps_color;    /* 0x14 color/depth-mode bits                 */
    uint32_t caps_misc;     /* 0x18 texture/misc                          */
    uint32_t max_something; /* 0x1c                                       */
    uint8_t  _pad[0x10];    /* 0x20..0x2f (shorts + a byte, see disasm)    */
    uint32_t video_memory;  /* 0x30 VRAM (CGL kCGLRPVideoMemory reads +0x30) */
    uint32_t texture_memory; /* 0x34 texture memory (kCGLRPTextureMemory reads +0x34) */
} gld_renderer_info;
typedef char gld_ri_chk_flags[(offsetof(gld_renderer_info, flags) == 0x08) ? 1 : -1];
typedef char gld_ri_chk_video_memory[(offsetof(gld_renderer_info, video_memory) == 0x30) ? 1 : -1];
typedef char gld_ri_chk_texture_memory[(offsetof(gld_renderer_info, texture_memory) == 0x34) ? 1 : -1];
#endif
