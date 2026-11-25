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

## Second round (G4): edge loop, affine, per-pixel helpers

All numbers are G4 MDD, gcc 3.3 -O2 -mcpu=7450, from `tests/bench/results_g4.txt` (fxbench) and `tests/bench/opsbench_results_g4.txt` (opsbench).

**Edge loop** (whole textured Gouraud pixel, loop included, ns per covered pixel): scanning the bounding box with a per-pixel 3-edge test 466-542 -> solving each row's covered range analytically
(one table reciprocal per edge, an exact one-step correction, no divide) 236-247 on large triangles; small triangles 12/24/40 px: 707->577, 593->416, 642->350. Coverage identical to the per-pixel
test (0 of 38400 rows differ).  Row extents also remove the per-pixel edge arithmetic entirely.

**Affine texture** (w exactly constant over the triangle): the texel position is a plane (w cancels), no divide, no table: 70 vs 105 ns net, error 0.004 texel (identical to the table path).  Gated on
exact w equality: with w varying by only 2% the error is 3.6 texel (10% -> 13.6), so no tolerance is used.

**Per-pixel helpers** (ns per call, before -> after, difference):
| helper | before | after | difference |
|---|---|---|---|
| texel fetch, repeat (index by mask + shift) | 12.8 | 7.3 | none |
| nearest sample | 55.5 | 26.0 | none |
| bilinear sample (two channels per 32-bit multiply) | 150 | 87 | max 1 LSB, mean 0.15 LSB per channel |
| env MODULATE / BLEND | 111 / 120 | 45 / 52 | none |
| env COMBINE (selectors resolved once per draw) | 422-450 | 130-160 | none |
| env REPLACE / DECAL / ADD | 91 | 33 | none |
| blend SRC_ALPHA,1-SRC_ALPHA / SRC_ALPHA,ONE / ONE,ONE (ADD) | 215-218 | 64 / 66 / ~55 | none |
| blend generic (factors resolved once per draw) | 243 | 190 | none |
Equality swept over all 405 (RGB565) / 810 (RGB555) source x destination x equation combinations.  Rejected: a 64 KB multiply table (cache cost); inlining pack/unpack alone (242 -> 233 ns).
