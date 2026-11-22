"""patch0: stock ATIRagePro.orig -> ATIRagePro.patched  (the 'peek' patch; reconstructed, not the author's binary)

Repurposes GL-context selector 14 (stock scale_surface, ScalarIStructI 3/0, unused by any client) into a
dual-mode kernel word read, as the driver (gld_render.c / gld_entry.c / tools/mach64_*peek*) expects:
    IOConnectMethodScalarIScalarO(conn, 14, 1, 1, arg, &out)
    arg bit31 clear : out = *(u32*)(this + arg)            (this = ATIR3GLContext)
    arg bit31 set   : out = *(u32*)(arg & 0x7fffffff)      (absolute kernel address)
Handler signature matches tools/mach64_findbase.c: [0x74808000, 0x4082000C, 0x7C03202E].
NOTE: arbitrary kernel read -- research scaffolding only; to be replaced by dedicated selectors."""
import struct, capstone
TB = 0x33c
SRC, DST = 'ATIRagePro.orig', 'ATIRagePro.patched'
VA = 0x710c
WORDS = [
    0x74808000,  # andis. r0,r4,0x8000
    0x4082000C,  # bne    +0xc  (absolute)
    0x7C03202E,  # lwzx   r0,r3,r4
    0x4800000C,  # b      +0xc  (store)
    0x5482007E,  # clrlwi r2,r4,1   (r2 scratch, as in the author's own binary)
    0x80020000,  # lwz    r0,0(r2)
    0x90050000,  # stw    r0,0(r5)
    0x38600000,  # li     r3,0
    0x4E800020,  # blr
]
STOCK_MD = (0, 0x4, 3, 0)   # method entry sel14: (word@0xc.. ) flags,c0,c1 stock = 4,3,0
data = bytearray(open(SRC,'rb').read())
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN)
# sanity: stock handler prologue and method entry
assert struct.unpack('>I', data[VA+TB:VA+TB+4])[0] == 0x7C0802A6, "not stock mflr at 0x710c"
me = 0x174c0 + 14*0x18 + TB
ent = struct.unpack('>6I', data[me:me+24])
assert ent[2] == VA and ent[3:6] == (4,3,0), "sel14 method entry is not stock: %r" % (ent,)
# no relocation may land inside the bytes we overwrite
end = VA + 4*len(WORDS)
reloff, n = 0x1aca4, 5533
for i in range(n):
    e = reloff+8*i; w0 = struct.unpack('>I', data[e:e+4])[0]
    a = (w0 & 0xffffff) if (w0 & 0x80000000) else w0
    assert not (VA <= a < end), "reloc at 0x%x inside handler" % a
for i,w in enumerate(WORDS): struct.pack_into('>I', data, VA+TB+4*i, w)
# method entry -> ScalarIScalarO (flags 0), 1 scalar in, 1 scalar out
struct.pack_into('>3I', data, me+12, 0, 1, 1)
open(DST,'wb').write(data)
for i in md.disasm(bytes(data[VA+TB:VA+TB+4*len(WORDS)]), VA): print("0x%05x: %-8s %s"%(i.address,i.mnemonic,i.op_str))
print("method entry now:", [hex(x) for x in struct.unpack('>6I', data[me:me+24])])
