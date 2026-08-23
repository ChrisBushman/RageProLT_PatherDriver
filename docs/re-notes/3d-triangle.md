# mach64 3D solid-triangle recipe

## ✅ WORKING on hardware (flat-shaded)

Verified on the Lombard: the setup engine rasterizes the correct triangle
shape, filled with a flat color.  Winning config:

```
SCALE_3D_CNTL = 0x100c1   (3D shade pipe on -> triangle coverage/rasterization)
SETUP_CNTL    = 0x40      Z_CNTL = 0   DP_PIX_WIDTH = 0x60060666 (ARGB8888)
DST_OFF_PITCH = ((pitch_px/8)<<22)|(offset>>3)   (off-screen render target)
DP_MIX        = 0x70007   (BKGD_MIX_S|FRGD_MIX_S)
DP_SRC        = 0x101     (FRGD/BKGD_SRC_FRGD_CLR -> color from DP_FRGD_CLR)
DP_FRGD_CLR   = color     SC_LEFT_RIGHT=0x1fff0000  SC_TOP_BOTTOM=0x3fff0000
per vertex n: VERTEX_n_Z=0, VERTEX_n_ARGB=color, VERTEX_n_X_Y=(x*4<<16)|(y*4&0xffff)
TRIGGER: ONE_OVER_AREA_UC(0x300) = floatbits(1.0/area)
```

- The **setup engine gives coverage**; color comes from **`DP_FRGD_CLR`** (flat).
  `SCALE_3D_CNTL=0` gives an empty result (3D pipe off), confirming the 3D pipe
  does the rasterization.
- **Gouraud (per-vertex color) is NOT working via MMIO**: with `DP_SRC=3D` the
  color reads `0xffffffff` regardless of `dp_mix`/`scale_3d_cntl`/ARGB order.
  The per-vertex ARGB doesn't latch into the interpolator through individual
  MMIO register writes — it needs the **DMA-burst vertex packet** (the DRI path:
  one packet writing Z/ARGB/X_Y as consecutive auto-incrementing registers).
  Future work once the bus-master ring is up.

Implemented in `tools/mach64_tri.c`.  Below is the original pre-hardware
analysis for reference.

---

# mach64 3D solid-triangle recipe (worked out from DRI, pre-hardware)

Analysis-only prep for the 3D triangle test.  Everything here is derived from
the DRI mach64 driver (`mesa_mach64_tris.c`, `mesa_mach64_state.c`, the reg
headers).  Goal: a flat/gouraud, untextured, no-depth triangle rasterized into
off-screen VRAM, verified by readback — same safety model as the 2D fill.

## Key correction vs. our host emit

`common/mach64_emit.c` assumed writing `VERTEX_3_X_Y` triggers the draw. **It
does not.** The mach64 setup engine needs a CPU-computed reciprocal area, and
the **trigger is the write to `ONE_OVER_AREA_UC` (0x0300)**, emitted *after* all
three vertices.  (Fix the host emit before wiring it to hardware.)

## Vertex register format (VERIFIED from the emit code)

Per vertex, three consecutive registers:

| reg | value |
|---|---|
| `VERTEX_n_Z`    (v1 `0x250`, v2 `0x270`, v3 `0x290`) | `((int)z) << 15`  → `0` for no depth |
| `VERTEX_n_ARGB` (`0x254`/`0x274`/`0x294`) | packed 0xAARRGGBB |
| `VERTEX_n_X_Y`  (`0x258`/`0x278`/`0x298`) | `((int)(x*4) << 16) \| ((int)(y*4) & 0xffff)` |

- **X in the high 16 bits, Y in the low 16**, each `coord * 4` (signed, 2
  sub-pixel bits).  Confirmed from `xx[i]=(int)(v.x*4); ... (xx<<16)|(yy&0xffff)`.
- The minimal (vertsize=4) vertex is exactly {Z, ARGB, X_Y} — **no W, no
  texture** needed for a solid triangle.  Registers are consecutive, so the
  DMA form is one packet `(2<<16)|ADRINDEX(VERTEX_n_Z)` + 3 dwords; for
  direct-MMIO we just write the 3 registers.

## The reciprocal area (REQUIRED, the trigger)

```
xx[i] = (int)(x[i]*4);  yy[i] = (int)(y[i]*4);        /* i = 0,1,2 */
area  = 0.25*0.25 * ((xx0-xx2)*(yy1-yy2) - (yy0-yy2)*(xx1-xx2));
/* backface: if (area * backface_sign < 0) cull -- pick winding so area > 0 */
ooa   = 1.0f / area;                                  /* IEEE float */
write ONE_OVER_AREA_UC (0x0300) = *(uint32_t*)&ooa;   /* <-- triggers the draw */
```

Note `0.25*0.25 = 1/16` converts the `*4`-scaled cross product back to pixel².

## 3D state to set once, before the triangle

| reg | value | meaning |
|---|---|---|
| `SCALE_3D_CNTL` `0x5fc` | **`0x100c1`** | `SCALE_PIX_EXPAND_DYNAMIC_RANGE(1<<0)` \| `SCALE_3D_FCN_SHADE(3<<6)` \| `ALPHA_BLEND_SRC_ONE(1<<16)` — gouraud, opaque, no tex/fog |
| `SETUP_CNTL` `0x304` | **`0x40`** | `FLAT_SHADE_OFF(0)` \| `SOLID_MODE_OFF(0)` \| `LOG_MAX_INC_ADJ(1<<6)` — the DRI init value (smooth). Flat: OR `FLAT_SHADE_VERTEX_3 = 3<<3`. RESOLVED |
| `Z_CNTL` `0x54c` | **`0`** | depth off — RESOLVED (`Z_EN` is bit 0, so 0 = disabled) |
| `DP_PIX_WIDTH` `0x6d0` | `0x60060666` | ARGB8888 datatype spread (32bpp target) |
| `DST_OFF_PITCH` `0x500` | `((pitch/8)<<22)\|(offset>>3)` | render target = off-screen VRAM (e.g. 0x600000) |
| `SC_LEFT_RIGHT` `0x6a8` | `(0x1fff<<16)\|0` | scissor wide open |
| `SC_TOP_BOTTOM` `0x6b4` | `(0x1fff<<16)\|0` | scissor wide open |
| `DP_WRITE_MASK` `0x6c8` | `0xffffffff` | |

(mesa's full default `SCALE_3D_CNTL` is `0xb4100cb` — adds dither + texture
bits; the texture bits are ignored under `FCN_SHADE`, so `0x100c1` is the clean
minimal value.)

## Submission (direct-MMIO, like the 2D fill)

`wait_for_idle` → set state → per vertex `wait_for_fifo(1)` + write {Z,ARGB,X_Y}
→ `wait_for_fifo(1)` + write `ONE_OVER_AREA_UC` (trigger) → `wait_for_idle` →
read back the covered pixels in off-screen VRAM.  Save/restore engine context
for coexistence, exactly as `mach64_fill.c` does.

## Uncertainties status

- ~~#1 `SETUP_CNTL`~~ — **RESOLVED**: `0x40` (DRI init value; no separate
  vertex-type field — the format is implicit in which VERTEX regs are written).
- ~~#2 `Z_CNTL=0` disables depth~~ — **RESOLVED**: yes (`Z_EN` = bit 0).
- ~~#4 trigger~~ — **RESOLVED**: `ONE_OVER_AREA_UC` (0x300), emitted last.

Minor items left (low risk, tune during the test — none can silently hang):
3. **`DP_MIX`/`DP_SRC`.** The 3D scaler path likely bypasses them; set them to
   the 2D-fill values as a safe default, or leave the engine's current state.
5. **Sub-pixel / fill-rule.** Choose large, clearly-interior sample pixels for
   the readback; ensure CCW winding so `area > 0` (else it culls).

## Ready to implement

The recipe is complete enough to build.  When we resume on hardware, write
`tools/mach64_tri.c` mirroring `mach64_fill.c` (idle-wait, save/restore context,
FIFO-gated writes, off-screen target, readback), using:

```
SCALE_3D_CNTL=0x100c1  SETUP_CNTL=0x40  Z_CNTL=0  DP_PIX_WIDTH=0x60060666
DST_OFF_PITCH=off-screen  scissor wide  DP_WRITE_MASK=0xffffffff
per vertex: VERTEX_n_Z=0, VERTEX_n_ARGB=color, VERTEX_n_X_Y=(x*4<<16)|(y*4&0xffff)
trigger:    ONE_OVER_AREA_UC(0x300) = floatbits(1.0/area)
```

Pick a big triangle (e.g. verts (32,32),(96,40),(48,110)) with one solid color
so any interior sample confirms it, and read back a couple of clearly-interior
pixels in the off-screen target.
