"""Convert this project's single-segment intermediate ELF to a two-segment PRX.

This intentionally rejects unsupported ELF features/relocations. It is not a
general-purpose replacement for a PS3 linker. No proprietary SDK is required.
Format reference: RPCS3's PPU PRX loader (PPUModule.cpp).
"""
from pathlib import Path
import os, struct as st, subprocess, sys
EH = '>16sHHIQQQIHHHHHH'
PH = '>IIQQQQQQ'
SH = '>IIQQQQIIQQ'

class Elf:
    def __init__(self, path):
        self.data=Path(path).read_bytes()
        self.h=st.unpack_from(EH,self.data)
        if self.h[0][:7]!=b'\x7fELF\x02\x02\x01' or self.h[2]!=21:
            raise ValueError('Expected big-endian ELF64 PowerPC')
        self.ph=[st.unpack_from(PH,self.data,self.h[5]+i*self.h[9]) for i in range(self.h[10])]
        self.sh=[st.unpack_from(SH,self.data,self.h[6]+i*self.h[11]) for i in range(self.h[12])]
        self.symbols={}
        for sec in self.sh:
            if sec[1]!=2: continue
            strings=self.section(self.sh[sec[6]])
            for pos in range(sec[4],sec[4]+sec[5],sec[9]):
                sym=st.unpack_from('>IBBHQQ',self.data,pos)
                name=strings[sym[0]:].split(b'\0',1)[0].decode()
                self.symbols[name]=sym[4]
    def section(self,s): return self.data[s[4]:s[4]+s[5]]

def pack(source, prx_path, sprx_path):
    e=Elf(source)
    loads=[p for p in e.ph if p[0]==1]
    if len(loads)!=1: raise ValueError('Only one LOAD segment is supported')
    p=loads[0]; _,flags,off,base,_,filesz,memsz,_=p
    if memsz>1024*1024: raise ValueError('Diagnostic module exceeds 1 MiB limit')
    payload=e.data[off:off+filesz]
    split=e.symbols['__presence_data_start']-base
    records=[]; seen=set()
    absolute={1,4,5,6,38}
    # Code-relative references stay within RX; TOC-relative references stay in RW.
    invariant={0,10,11,26,44,47,48,49,50,63,64,249,250,251,252}
    for sec in e.sh:
        if sec[1]!=4: continue
        target=e.sh[sec[7]]
        if not target[2]&2: continue
        syms=e.sh[sec[6]]
        for pos in range(sec[4],sec[4]+sec[5],24):
            at,info,addend=st.unpack_from('>QQq',e.data,pos)
            kind=info & 0xffffffff
            sym=st.unpack_from('>IBBHQQ',e.data,syms[4]+(info>>32)*syms[9])
            if kind in invariant: continue
            if kind==51:
                kind=38; value=e.symbols['.TOC.']+addend
            elif kind in absolute:
                if not sym[3]: raise ValueError('Unresolved external symbol')
                value=sym[4]+addend
            else:
                raise ValueError(f'Unsupported relocation {kind} at {at:x}')
            if not base<=at<base+filesz: raise ValueError('Relocation location outside image')
            width=8 if kind==38 else 4 if kind==1 else 2
            if at+width>base+filesz: raise ValueError('Relocation overruns image')
            if at in seen: raise ValueError('Duplicate relocation')
            seen.add(at)
            if sym[3]==0xfff1 and kind!=51 and not base<=value<base+memsz+0x8000:
                index=255; relative=value
            else:
                index=1 if value>=base+split else 0
                relative=value-base-(split if index else 0)
                if relative<0 or relative>memsz+0x8000: raise ValueError(f'Address outside module: {value:x}')
            address_index=1 if at>=base+split else 0
            records.append(st.pack('>QHBBIQ',at-base-(split if address_index else 0),0,index,address_index,kind,relative))
    relocation=b''.join(records)
    module=e.symbols['presence_module_info']-base
    image_offset=0xf0
    reloc_offset=(image_offset+filesz+15)&~15
    ident=b'\x7fELF\x02\x02\x01\x66'+bytes(8)
    header=st.pack(EH,ident,0xffa4,21,1,0,64,0,0x01000000,64,56,3,0,0,0)
    ph0=st.pack(PH,1,0x400005,image_offset,0,image_offset+module,split,split,16)
    ph1=st.pack(PH,1,0x600006,image_offset+split,split,0,filesz-split,memsz-split,16)
    ph2=st.pack(PH,0x700000a4,0,reloc_offset,0,0,len(relocation),0,16)
    result=header+ph0+ph1+ph2
    result+=bytes(image_offset-len(result))+payload
    result+=bytes(reloc_offset-len(result))+relocation
    Path(prx_path).write_bytes(result)
    tool=Path(os.environ.get('PS3DEV','/opt/ps3dev'))/'bin'/'fself'
    subprocess.run([str(tool),prx_path,sprx_path],check=True)
    # Use the conventional VSH-plugin auth ID in the unsigned debug SELF.
    wrapped=bytearray(Path(sprx_path).read_bytes())
    appinfo=st.unpack_from('>Q',wrapped,0x28)[0]
    st.pack_into('>Q',wrapped,appinfo,0x1070000052000001)
    Path(sprx_path).write_bytes(wrapped)
    print(f'Packed {filesz} file bytes, {memsz} resident bytes, {len(records)} relocations; unsigned development SPRX.')

if __name__=='__main__':
    if len(sys.argv)!=4: raise SystemExit('usage: pack_prx.py input.elf output.prx output.sprx')
    pack(*sys.argv[1:])
