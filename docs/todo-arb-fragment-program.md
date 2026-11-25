# TODO: ARB_fragment_program (deferred, not blocking)

Status: GLEngine accepts `glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, ...)` and `glEnable(GL_FRAGMENT_PROGRAM_ARB)`, but the driver never runs the program; draws come out with the primary colour (probe `fragment_program` fails).

## What static analysis established (Panther GLEngine, `~/gleng_panther/glengine.dis`)
- Program string handler: 0x9cfa0 (args: gli, program object, target flag; flag 1 = fragment). It builds a `PPParser`, calls `PPParserParse` (0x9d104) and stores the resulting `PPStream*` at program-object+0.
- For vertex programs only, 0x9e468..0x9e498 also builds a `PPEmulatorProgram` (`PPEmulatorProgramCreate` + `ConvertFromPPStream`) stored at object+0x4e0 and runs it with `PPEmulatorRun` (wrapper 0xd9908). Fragment programs skip that step: only the PPStream exists.
- Program object for a target: `*(gli_base + 0x10000 + 0x8724 + 4*idx)`, idx 0 = vertex (0x8620), 1 = fragment (0x8804). Currently bound program: `gli_base+0x10000+0x8734`.
- Parse errors land in object+0x34 (600-byte string) and the error position in the parser (GL_PROGRAM_ERROR_STRING_ARB).
- The program text is not retained where the driver could read it.

## Routes
a) Reuse Apple's emulator (`/System/Library/Frameworks/OpenGL.framework/Libraries/libGLProgrammability.dylib`; `~/rage_glprog/pdis.py` disassembles it with capstone). Exports: PPEmulatorCreate/Init/Run/Build, PPEmulatorProgramCreate/ConvertFromPPStream/InitialiseHandleBanks. Emulator struct fields seen: +0x2d0 program, +0x2d8 output blocks, +0x2dc input blocks, +0x2e0 constants bank, +0x2e4 gli+0x3610, +0x2ec program+0x4c; per-element blocks use a 0x100-byte stride (see GLEngine 0xd9908). It also has `PPEmulatorSecondaryThread`, which conflicts with the single-core rule, and every field must be reverse-engineered.
b) Decode the PP stream chunk format (`PPStreamChunkCreate/ListAddChunk`; chunks of 0x30 bytes with 4 operand slots, seen in GLEngine 0x9e374..0x9e458) and write a fixed-point/single-precision interpreter inside the driver CPU rasteriser (`gld_stencil.c`, env hook like `env_apply_ref`). Preferred: no thread, no dlopen dependency.

## Work items
1. Decode chunk opcodes/operands/swizzles/masks from `PPEmulatorEmulateOp` (libGLProgrammability 0x810f7480, ~0x1a00 bytes) and `PPEmulatorLoadSource/WriteDestination`.
2. Interpreter for ARBfp1.0: MOV ADD MUL MAD DP3 DP4 LRP LIT RCP RSQ POW EX2 LG2 FRC FLR ABS CMP SUB SWZ XPD TEX/TXP/TXB KIL, saturate, condition-free; inputs fragment.color/texcoord[n]/position, program.local/env parameters (GLEngine state), outputs result.color/depth.
3. Per-pixel plumbing: texture sampling for any bound unit (CPU sampler already exists, level 0 only), colour/texcoord planes for all units.
4. Route draws to the CPU path when a fragment program is enabled; probe `fragment_program` is the first test, then add TEX, KIL and parameter probes.
