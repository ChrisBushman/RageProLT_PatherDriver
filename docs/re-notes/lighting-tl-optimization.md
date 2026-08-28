# TODO / DEFERRED: simplifying GL_LIGHTING cost on the G3 (design note, 2026-08-27)

Idea (user): GLEngine does GL_LIGHTING in software and the G3 (PPC 750, no AltiVec) is terrible at
the per-vertex float math -- especially specular pow(N.H, shininess).  Can the driver simplify that
math before the CPU pays for it?

## The hard constraint: we are DOWNSTREAM of lighting
GLEngine does ALL of T&L (transform, lighting, clip, project, assemble) in software and hands our
renderer FINISHED, screen-space, ALREADY-LIT vertices.  Evidence: the gli-vertex we receive
(gld_prims.c) carries a final R/G/B/A at +0x30..0x3c and NO normal -- the normal was already consumed
by lighting before our hooks fire.  So from the render side the expensive work is already spent; there
is nothing left to simplify.  And there is no GPU to offload to (mach64 is rasteriser-only, no HW T&L).

## The only two intervention points (both big)
1. **Claim hardware-TCL capability.**  GLEngine would then hand us raw object-space vertices + light
   state and expect us to do EVERYTHING (transform, lighting, clip, viewport, assembly).  We'd control
   the math, but clipping alone is a monster, and on a G3 it only wins if we approximate (fixed-point,
   drop specular/attenuation), changing the image.  Effectively rewriting GLEngine's front end.
2. **Interpose libGL in FRONT of GLEngine (the real version of the idea).**  A DYLD shim that tracks
   lighting state and, for the common FAST CASE (<=1 directional light, diffuse-only, unit normals),
   computes per-vertex colour with a cheap hand-tuned N.L -- no specular pow, no 8-light loop, no
   attenuation -- sets the colours, disables GL_LIGHTING, and lets GLEngine do only the transform.
   Everything else passes through untouched.  Bypasses GLEngine's general lighting path with a
   specialised cheap one.  Caveats: it's a separate component (NOT the renderer), app-global, fragile;
   a real GL-lighting state machine to build; still CPU float (win = our path cheaper than GLEngine's);
   subtly changes output.

## Recommendation: MEASURE THE PRIZE FIRST
Before either big path, quantify how much of the ~3fps is LIGHTING vs unavoidable TRANSFORM: a probe
(reuse GLD_PERF) that draws the SAME geometry lit vs unlit and reports the frame-time delta.  If
lighting is ~60% -> the interpose fast-path (#2) is worth scoping; if ~15% -> not worth it.  The cheap
real levers are app-side anyway (fewer lights, unit normals + GL_NORMALIZE off, no specular, bake
lighting into vertex colours), which the driver can't force.

STATUS: deferred.  Next actionable step = build the lit-vs-unlit measurement probe.
