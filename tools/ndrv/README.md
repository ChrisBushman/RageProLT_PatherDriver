# tools/ndrv - reading Apple's ATI NDRV (the Lombard's framebuffer driver)

Apple's framebuffer for the Rage LT Pro is a Mac OS PEF "NDRV" run by `IONDRVFramebuffer`. A copy ships on every Panther install:
`/System/Library/Extensions/AppleNDRV/ATIDriver.bundle/Contents/MacOS/ATIDriver` (1.3.42): 44 PEF containers back to back, one per chip family.
**Do not commit the binary or its decompilation (Apple's); the findings are in docs/re-notes/apple-ndrv-framebuffer.md.**

1. Split: find every `Joy!peff` in the file; the container whose strings contain `ATY,264LTPro` is the one for 1002:4c49 (offset 0x88ad0, length 0x1d9c0 in 1.3.42).
2. Import into Ghidra as **PowerPC:BE:32:default** (the auto-selected 64-bit VLE language gives poor output): `analyzeHeadless proj name -import ndrv.pef -processor PowerPC:BE:32:default`.
3. `DumpDecompiled.java` (postScript, argument = output dir) writes `funcs.c` and `index.txt`.
4. Ghidra leaves the PEF data section packed; `pefdata.py` unpacks it (pattern-initialised data, ops 0-4) so TVectors and the TOC can be read. Exports: `TheDriverDescription`, `SerialOut`, `DoDriverIO` (data offset 0x68c -> code 0x5030).
