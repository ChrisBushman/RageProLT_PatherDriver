# GLDriver translation layer — status

The userspace `gld*` plugin that turns GL calls into the register streams the
kext submits.  Sources: `gldriver/gld_context.c` (handshake), `gld_state.c` (GL
state → registers), `gld_render.c` (geometry + submission), reusing the portable
`common/mach64_emit.c` stream builder.

## ✅ PROVEN on hardware (mach64_gldtest)

The translation core draws a gouraud triangle through the real plugin code end
to end, bypassing only Apple's GLEngine:

```
mach64_init_state / mach64_set_render_target   (GL state -> m64_state shadow)
mach64_pack_vertex x3                           (post-T&L float -> hw vertex)
mach64_draw_triangle                            (emit state+tri+ooa trigger)
  -> m64_cb_write_le  (host BE -> LE bytes in the type-4 command buffer)
  -> new_vertex_buffer -> kext rotateCommandBuffer -> DMA ring
gldFinish -> wait_for_stamp
```

Readback was **byte-identical** to the hand-built `mach64_ringtest` stream
(near-v0=ff8a7102, v1=ff1dbf22, v2=ff1e04db, centroid=ff63405a, covered=507) —
so the emit path produces exactly the verified 50-dword/200-byte stream.

Key facts locked in by this:
- vertex = one 7-register burst {S,T,W=1.0,SPEC,Z,ARGB,X_Y}; the LE byte-swap
  lives in the plugin (`m64_cb_write_le`), NOT the kext.
- `mach64_priv` backend (shadow state + dirty + last_stamp) hangs off
  `gld_context.backend`; state re-emitted only when dirty.

## ⛔ Still stubbed: the GLEngine <-> gld dispatch ABI

The one piece we can't derive from our own hardware work.  Unknowns:
- what `gldUpdateDispatch` installs (function-pointer slots + order),
- how GLEngine hands vertices to the plugin (array layout, the
  Alloc/CompleteVertexBuffer calling convention),
- exact `gldGetRendererInfo` caps struct + `gldChoosePixelFormat` attribs.

**Reference for this:** `ATIRage128GLDriver.bundle/Contents/MacOS/
ATIRage128GLDriver` (Mach-O bundle ppc, on /Volumes/macUSB) — the sibling driver
that DOES ship a GL bundle.  Disassemble its `gldUpdateDispatch` + vertex entry
points to recover the ABI, then wire GLEngine's primitive callbacks to
`mach64_draw_triangle`.  That turns the proven core into a loadable bundle.
