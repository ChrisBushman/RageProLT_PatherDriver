# Reference only - not built

`mach64_pathb_suite.c` is the 178-check headless suite from the old Path-B plugin. It no longer builds (the plugin and the
Path-A/B kernel interfaces it called were deleted on this branch). It is kept as the **list of behaviours to port** to the new
driver's own harness in Phase 3 (state, Z, blend, texture, DMA, guards...). Old sources: branch `sonicr-driver-import`.

Known suite bugs fixed on the way: y-flip must use the surface height (`ctx+0x1f4`), not 256 (see docs/standalone-driver-design.md 11.7).
