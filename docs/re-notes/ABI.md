# Reverse-engineered ABI: Panther GL driver stack (mach64 / Rage 128)

Everything here was recovered by disassembling the shipping **Panther
`ATIRage128*`** driver set (the sibling of our target that *has* a GL driver)
plus the open-source DRI/DRM mach64 driver. Tools: `nm`, custom Mach-O parser
+ Capstone PPC (`../` build scripts), `c++filt`.

## Three-tier stack (from JUCE crash traces + bundle layout)

```
OpenGL.framework / libGL.dylib     CGL*, gl*        public API
GLEngine.bundle                    gli*, gle*       Apple's shared GL engine (owns state machine, gleUpdateState)
<vendor>GLDriver.bundle            gld*             vendor backend  <-- WE WRITE THIS
<vendor>.kext (IOUserClient)       C++ IOKit        kernel driver   <-- WE EXTEND ATIRagePro.kext
```

Our target chip ships `ATIRagePro.kext` + `ATIRageProGA.plugin` but **no
`ATIRageProGLDriver.bundle`** — that missing bundle is the project.

## Plugin exports (GLDriver bundle): 55 `gld*` symbols

Full lifecycle. Key ones (disassembled): `gldInitializeLibrary`,
`gldGetRendererInfo`, `gldChoosePixelFormat`, `gldCreateContext`,
`gldUpdateDispatch`, `gldAlloc/Complete/FreeVertexBuffer`, `gldFlush/Finish`,
`gldCreate/Modify/DeleteTexture`. Stub on fixed-function mach64:
`gldCreatePipelineProgram*` (no shaders), `gldCreateQuery*` (no occlusion).
No `gldSwapBuffers` — present via `gldFlushBuffer` + window server.

### `gldGetRendererInfo` caps struct (offsets)

| off | field | Rage128 value |
|---|---|---|
| 0x08 | renderer_id | 0x2513 |
| 0x10 | caps (accel + buffer modes) | 0x8400 |
| 0x14 | color/depth modes | 0x808000 |
| 0x18 | misc caps | 0x1c01 |
| 0x30 / 0x34 | VRAM / texture mem (bytes) | queried |

### `gldCreateContext` handshake (verbatim)

```
malloc(0x200)                                     ; 512-byte context
IOServiceOpen(svc, task, type=1, &ctx[0x04])      ; conn -> ctx[0x04]
[IOConnectAddClient for share group]
io_connect_method_scalarI_scalarO(sel=3, 0->3)    ; get_config -> ctx[0x34..0x3c]
IOConnectMapMemory(type=0 -> ctx[0xdc], opt 0x101) ; registers
IOConnectMapMemory(type=1 -> ctx[0xb8])            ; VRAM surface
IOConnectMapMemory(type=2 -> ctx[0xc0])            ; control page
IOConnectMapMemory(type=4 -> ctx[0xd0])            ; command buffer
malloc(size_of_type4 / 32)                         ; command-buffer alloc bitmap
```

## Kext contract (IOATIR128GLContext)

### `clientMemoryForType` (jump table @0x5780) — VERIFIED roles

| type | role | how the kext returns it |
|---|---|---|
| 0 | **MMIO registers** | member `IODeviceMemory` [hw+0x9c], opts=0 |
| 1 | **VRAM render surface** | dynamic allocator on surface obj |
| 2 | **4KB control page** | member region [this+0xd0] |
| 3 | second aperture (unused by GL) | member [hw+0xa0] |
| 4 | **growable command buffer** | `IOBufferMemoryDescriptor::withOptions(0x10023)` |

> Correction: an earlier guess had type 1 = registers. The disassembly shows
> **type 0 = registers**, type 1 = VRAM surface. Always reverse, don't assume.

### 17 `io_connect_method` selectors (`getTargetAndMethodForIndex`)

Table A `[this+0x11c]` idx 0–14, Table B `[this+0x134]` idx 15–16 (−0x168):

| sel | method | dispatch | in | out |
|---|---|---|---|---|
| 0 | set_surface | ScalarI_StructI | 4 | – |
| 1 | set_swap_rect | ScalarI_StructI | 4 | – |
| 2 | set_swap_interval | ScalarI_StructI | 2 | – |
| **3** | **get_config** | ScalarI_ScalarO | 0 | 3 |
| 4 | get_status | ScalarI_ScalarO | 0 | 1 |
| 5 | get_surface_size | ScalarI_ScalarO | 0 | 4 |
| 6 | get_surface_info | ScalarI_ScalarO | 1 | 3 |
| 7 | read_buffer | StructI_StructO | var | – |
| 8 | finish | ScalarI_StructI | 0 | – |
| 9 | wait_for_stamp | ScalarI_StructI | 1 | – |
| 10 | new_texture | StructI_StructO | var | var |
| 11 | delete_texture | ScalarI_StructI | 1 | – |
| 12 | become_global_shared | ScalarI_StructI | 1 | – |
| 13 | pageoff_texture | ScalarI_StructI | 2 | – |
| 14 | scale_surface | ScalarI_StructI | 3 | – |
| 15 | new_vertex_buffer | ScalarI_ScalarO | 1 | 1 |
| 16 | delete_vertex_buffer | ScalarI_StructI | 1 | – |

### Submission model

No "submit" selector. The plugin writes commands into the type-4 shared
buffer; **`new_vertex_buffer` (15)** hands back the next buffer AND kicks the
previous; **`wait_for_stamp` (9)** fences on completion. `process_command_buffer`
(internal) interprets vendor packets (opcode = top byte of each header word)
and emits **byte-reversed (`stwbrx`) LE register writes** to the engine — this
is the endianness-conversion + validation boundary. For the mach64 port we use
the simpler DRI model: the plugin emits verified `(ADRINDEX(reg), value)` pairs
(see `common/mach64_emit.c`), the kext byte-reverses and submits.

## Register offsets

3D engine registers (stripped from the "Restricted" RRG) recovered from the
DRI header — see `common/mach64_regs.h`, cross-checked against
`docs/dri-reference/drm_mach64_drv.h`.
