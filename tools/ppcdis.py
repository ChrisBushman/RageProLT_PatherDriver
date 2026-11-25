import sys,struct,capstone
d=open(sys.argv[1],'rb').read(); lo=int(sys.argv[2],16); hi=int(sys.argv[3],16)
magic,cpu,sub,ft,nc,sz,fl=struct.unpack('>7I',d[:28]);off=28
secs=[];syms=[];stroff=0
for _ in range(nc):
    cmd,cs=struct.unpack('>II',d[off:off+8])
    if cmd==1:
        nsects=struct.unpack('>I',d[off+48:off+52])[0];so=off+56
        for i in range(nsects):
            sn=d[so:so+16].rstrip(b'\0').decode()
            addr,size,offs,align,reloff,nreloc,flags=struct.unpack('>7I',d[so+32:so+60])
            secs.append((sn,addr,size,offs,reloff,nreloc));so+=68
    if cmd==2:
        _,_,symoff,nsyms,stroff,strsz=struct.unpack('>6I',d[off:off+24])
        for i in range(nsyms):
            n,t,s,desc,val=struct.unpack('>IBBhI',d[symoff+i*12:symoff+i*12+12])
            syms.append((d[stroff+n:d.index(b'\0',stroff+n)].decode('latin1'),t,s,val))
    off+=cs
sec=secs[0]  # __text
rel={}
for i in range(sec[5]):
    r0,r1=struct.unpack('>II',d[sec[4]+i*8:sec[4]+i*8+8])
    if r0&0x80000000:
        continue
    a=r0; symnum=r1&0xffffff; ext=(r1>>27)&1
    if ext and symnum<len(syms): rel[a]=syms[symnum][0]
byval={}
for n,t,s,v in syms:
    if s==1 and (t&0xe)==0xe: byval[v]=n
md=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN)
code=d[sec[3]+lo-sec[1]:sec[3]+hi-sec[1]]
for i in md.disasm(code,lo):
    note=rel.get(i.address,'')
    if not note and i.mnemonic in('bl','b') :
        try:
            t=int(i.op_str,16); note=byval.get(t,'')
        except: pass
    print('%x: %-8s %s  %s'%(i.address,i.mnemonic,i.op_str,('; '+note) if note else ''))
