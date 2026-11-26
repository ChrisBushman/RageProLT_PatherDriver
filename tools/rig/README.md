# tools/rig - helpers for the G4 test rig (Panther 10.3.9, ssh alias `MDD`)

Run the `.sh` helpers from a Linux/dev host unless noted; the `.c` tools and the shell scripts marked (G4) are built/run on the rig.

| File | Purpose |
|---|---|
| `redeploy.sh N` | sync the tree to `~/rage-p1` on the G4, build the **debug** kext (`RAGE_DEBUG_ENGTEST=1`), checkfp, reload it, restart the `ragelt` daemon. Needs `RAGELT_LOG_HOST=ip:port`. |
| `inst_bundle.sh` | sync `gldriver`+`common`, rebuild and install `ATIRageProGLDriver.bundle` on the G4 |
| `bisect_dirty.sh` | bisect which `gl_probe` test group leaves the engine dirty |
| `rungui.sh OUT SECS "cmd"` (G4) | run a GUI/GL program inside the console session with a timeout, output to OUT |
| `drag.c key.c rinfo.c peek14.c openprobe.c lodscan.c fpscount.c sleepgran.c` | small rig utilities (synthetic input, register/PCI peeks, LOD scan, fps counter, sleep granularity) |
| `cyc*.sh soak.sh stress*.sh gl_loop.sh repaint_loop.sh overlap*.sh saver1.sh nosaver.sh leak.sh heartbeat.sh combo.sh fpsgdb.sh loadtest.sh agg*.awk` | soak / stress / leak / cycle loops and log aggregation used in the earlier driver bring-up (`loadtest.sh` needs `RAGELT_LOG_IP`) |

Display mode tools are in `rig/stubfb` (`cgmode`, `stubctl`) and `tools/mach64_arrange.c`; GL test programs in `tools/ragegl`. **Never run the old `bringup.sh`** (it lives on branch `sonicr-driver-import`): Apple's `ATIRagePro` shares the IOMatchCategory with this driver, so the standalone kext only attaches with the boot stub (`RageStubFB`) and `redeploy.sh`.
