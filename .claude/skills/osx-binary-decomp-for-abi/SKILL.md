---
name: osx-binary-decomp-for-abi
description: Concrete decompilation recipes for extracting ABIs and contracts from shipped OS X PPC Mach-O binaries (frameworks, kexts, driver bundles) — resolving symbol stubs, chasing PIC references, mapping runtime↔file offsets, recovering struct layouts from store patterns, decoding jump tables, and porting decode/encode logic to C. Use when you have Apple's binary and need the exact byte format / function contract / struct offsets it implements, not just a rough disassembly.
---

# Decompiling OS X resources to derive ABIs

The reusable Capstone-PPC recipes that turned Apple's stripped OpenGL binaries
into a working driver. Set up a venv with `capstone`; disassemble big-endian
32-bit: `Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32)`.

## Parse the Mach-O yourself (old binaries break modern otool)

Modern `otool`/`nm` often fail on old PPC Mach-O ("symbol table extends beyond
end of file"). Parse the load commands directly (big-endian, magic `feedface`):
walk `LC_SEGMENT` (segname, vmaddr, fileoff, filesize, and per-section
addr/offset/reserved1/reserved2), `LC_SYMTAB` (symoff/nsyms/stroff), `LC_DYSYMTAB`
(indirectsymoff). The **target's own** `nm`/`otool` (native, era-appropriate)
still works when the host's doesn't — use it for a quick symbol list.

## Resolve a symbol stub (`bl 0xNNNN` into a `__picsymbolstub1`)

A `bl` into a `mflr/bdnzl/addis/lwzu/mtctr/bctr` block is a lazy stub. Map it to
the imported name:
- stub index = `(stub_foff − stub_section_fileoff) / stub_size` (size is the
  section's `reserved2`, e.g. 0x20).
- indirect index = `section.reserved1 + stub_index`.
- symbol index = `indirect_symbols[indirect_index]`.
- name = string table at `symtab[symbol_index].n_strx`.

This is how `bl 0xa78c` resolved to `_glcAddFormatToCache` — the difference
between "some function" and "the format cache".

## Map a runtime address to a file offset (and back)

From a crash log or `__builtin_return_address(0)`, you have a runtime address; the
binary is at a **load base** listed in the crash "Binary Images" (or derived: if
you know one function's runtime addr and its file offset, base = runtime − foff).
For a bundle's `__text`, foff == vaddr, so `foff = runtime − base`. This is the
single most-used operation — every "who called me" and "what's at this pointer"
goes through it.

## Recover a struct layout from store patterns

To learn a struct's field offsets, disassemble the function that *builds* it and
enumerate every `stw/sth/stb rX, disp(rBASE)` where `rBASE` holds the struct
pointer. The set of `disp` values IS the layout; the source registers tell you
what each field is (map-call result → a mapped region ptr, get_config output →
device info, a constant → a flag/type code). We recovered the whole 0x200-byte GL
context this way, and the dispatch table's slot map by enumerating stores to the
table register.

Corollary — **comments lie, offsets don't**: a struct whose C definition lacks
padding compiles its fields to the wrong offsets even if the comments say the
right ones. Recover offsets from the binary and pad to match; compile-assert the
size.

## Decode a jump table (switch on a token/mode)

`slwi r9, rTOKEN, 2; addi rBASE, rPIC, off; lwzx r0, r9, rBASE; add r0,r0,rBASE;
mtctr r0; bctr` is a PIC jump table: base at `rPIC+off`, entries are
self-relative offsets. Read the table words, add the base, and you have every
case handler. This is how the pixel-format attribute parser's per-token handlers
were found.

## Port a decode/encode routine to C, faithfully

When a routine converts A→B (e.g. color/depth size → node bitfield, or node →
caps flags), transcribe it branch-for-branch rather than paraphrasing — the exact
thresholds and constants matter (`size > 0x20 → 0x3ff00000`, `node[0x08] & 0x100`,
`node[0x14] == 0x800000 → 32bpp`). A "close enough" port produces subtly wrong
values that the consumer silently rejects. Keep the reference offsets in comments
so the next person can re-verify against the binary.

## Find a global's writers (who populates a table/registry)

To find where a runtime-populated global (a registry head, a config table) is
filled, you can't grep the absolute address — it's referenced via PIC. Track, per
function, the PIC base (`mflr rN` after a `bcl .+4`), the `addis rM, rN, HI`
derived registers, and then any `stw/lwz rX, LO(rM)` whose computed
`base + (HI<<16) + LO` equals your target. (When that scan misses, fall back to a
runtime watchpoint on the address once the image is mapped.)

## Turn the disassembly into runtime captures

Static RE finds *where* to look; a gdb `-batch -x` script reads the real values:
`break *0xADDR`, then in `commands` dump registers and walk pointers with
`printf` + a `while` loop. Break just after a call to read a function's *return
value* and the caller's next comparison — that's how "this callback must return
0" was found. See [[osx-gl-framework-integration]] and [[driver-dev-test-loop]].

## Reusable scaffolding

Keep these as scratch scripts (they were rebuilt many times this project):
- Mach-O parser → segments, symtab, indirect syms, stub section.
- `foff(vaddr)` / `vaddr(foff)` / `name(vaddr)` / `stub_name(foff)` helpers.
- A disassemble-window function that annotates `bl`/`bctrl` targets with resolved
  names and flags stores to a register of interest.

See also [[ppc-macho-reverse-engineering]], [[osx-driver-info-sources]].
