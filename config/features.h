/* config/features.h - build-time feature switches for the Rage LT Pro driver (docs/standalone-driver-design.md 3.2).
 * Override on the compiler line, e.g.  -DRAGE_FEAT_DVD=0.  A feature set to 0 removes its classes, selectors and plist keys.
 * 2D acceleration is NOT optional: software 2D is not acceptable (user, Nov 24), so RAGE_FEAT_2D_GA exists only so that
 * early skeleton builds can omit it; release builds must leave it 1 (checked by `make features-check`). */
#ifndef RAGE_FEATURES_H
#define RAGE_FEATURES_H

#ifndef RAGE_FEAT_PANIC_LOG
#define RAGE_FEAT_PANIC_LOG   1   /* RAM ring + file + UDP diagnostics                                   (Phase 2) */
#endif
#ifndef RAGE_FEAT_2D_GA
#define RAGE_FEAT_2D_GA       1   /* stock-compatible 2D context for ATIRageProGA.plugin                 (Phase 5) */
#endif
#ifndef RAGE_FEAT_SLEEPWAKE
#define RAGE_FEAT_SLEEPWAKE   1   /* power management; Lombard only, unverifiable on the G4 rig          (Phase 5) */
#endif
#ifndef RAGE_FEAT_DMA
#define RAGE_FEAT_DMA         1   /* bus-master ring for commands and texture upload; 0 = PIO only       (Phase 1+) */
#endif
#ifndef RAGE_FEAT_OVERLAY
#define RAGE_FEAT_OVERLAY     1   /* video overlay (stock has none)                                      (Phase 5) */
#endif
#ifndef RAGE_FEAT_SCALER
#define RAGE_FEAT_SCALER      1   /* hardware scaler / full-screen upscale                               (Phase 5) */
#endif
#ifndef RAGE_FEAT_DVD
#define RAGE_FEAT_DVD         1   /* DVD motion-compensation assist (stock has none)                     (Phase 5) */
#endif
#ifndef RAGE_FEAT_FS_SCALE
#define RAGE_FEAT_FS_SCALE    1   /* full-screen 32 bpp inside 8 MB VRAM (bug L4)                        (Phase 3/5) */
#endif
#ifndef RAGE_DEBUG_PANIC
#define RAGE_DEBUG_PANIC      0   /* DEBUG ONLY: a selector that panics on purpose, to test what the capture paths preserve. Never in release. */
#endif
#ifndef RAGE_DEBUG_TRACE
#define RAGE_DEBUG_TRACE      0   /* verbose, fsync'd trace; off in release                                        */
#endif

/* feature bits as reported by the kext (values in common/rage_abi.h) */
#define RAGE_FEATURE_BITS ((RAGE_FEAT_PANIC_LOG ? 0x01u : 0u) | (RAGE_FEAT_2D_GA ? 0x02u : 0u) | (RAGE_FEAT_SLEEPWAKE ? 0x04u : 0u) | \
                           (RAGE_FEAT_DMA ? 0x08u : 0u) | (RAGE_FEAT_OVERLAY ? 0x10u : 0u) | (RAGE_FEAT_SCALER ? 0x20u : 0u) | \
                           (RAGE_FEAT_DVD ? 0x40u : 0u) | (RAGE_FEAT_FS_SCALE ? 0x80u : 0u))

/* The FPU is never used in the kernel (design 2). */
#define RAGE_KERNEL_NO_FP 1

#endif
