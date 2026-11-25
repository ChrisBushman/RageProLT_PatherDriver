# CPU compositing: math selection (G4 MDD, gcc 3.3, -O2 -mcpu=7450)

Rule applied: integer maths wherever possible, no double, lookup tables with interpolation, then fixed point, single float only if precision loss is too great.
Method: each candidate measured in isolation (`tests/bench/fxbench.c`, raw output `tests/bench/results_g4.txt`) for speed (net ns per covered pixel, shared edge loop subtracted)
and precision (against a double-precision reference that exists only in the test).  Shared edge loop itself: 140-225 ns/px.

| per-pixel stage (net ns/px, large triangles) | float (was) | float, int32 weights | integer: planes + 256-entry reciprocal table | max error |
|---|---|---|---|---|
| perspective texture coords | 300 | 185 | **115-120** | 0.004-0.007 texel (float: 0.004) |
| vertex colour | 620 (int64 divide) | - | **140-155** (incremental planes) | 0.5 LSB (identical) |
| cube-map face + coords | 370 | - | **130-140** | 0.004-0.009 texel (float: 0.004) |
| affine texture (w constant; not yet a separate path) | - | - | 50 | 0 |

Small triangles (12-40 px across) with the integer path (setup uses one table reciprocal + one Newton step, no 64-bit divide): texture 138-176, colour 169-208, cube 159-196 ns/px, still below float (186 / 620 / 380).

Rejected: span subdivision (exact every 4/8/16 px, linear between): 4-px spans max error 3.6 texel, 16-px up to 12 texel (mean 0.5) on moderate perspective, 393 texel strong perspective - precision loss too great for the gain.
Remaining float in the CPU paths: per-vertex conversion of GLEngine's float attributes to fixed point (3 per triangle), per-draw constants (blend constant colour, env colour), per-upload border colours.
