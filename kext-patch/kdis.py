import sys, capstone
TEXT_BIAS = 0x33c   # __text: file_off = vaddr + 0x33c
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN)
def disasm(path, vaddr, n):
    data = open(path,'rb').read()
    foff = vaddr + TEXT_BIAS
    code = data[foff:foff+n*4]
    for ins in md.disasm(code, vaddr):
        print("0x%05x: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
if __name__=='__main__':
    path=sys.argv[1]; va=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 40
    disasm(path, va, n)
