"""Validate structure and simulate relocations at independent load addresses.
Hardware loading is a separate test; this does not claim to emulate VSH.
"""
from pathlib import Path
import struct as st, sys
from pack_prx import Elf

def verify(original_path,prx_path,sprx_path):
    original=Elf(original_path); e=Elf(prx_path)
    assert e.h[1]==0xffa4 and len(e.ph)==3 and e.h[7]==0x01000000
    load,data,rel=e.ph
    assert load[0]==1 and rel[0]==0x700000a4 and rel[5]%24==0
    assert load[2]+load[5]<=len(e.data) and rel[2]+rel[5]==len(e.data)
    original_base=next(p[3] for p in original.ph if p[0]==1)
    split=data[3]
    for base,dbase in ((0x10000000,0x18000000),(0x18000000,0x10000000)):
        bases=(base,dbase)
        memory=bytearray(e.data[load[2]:load[2]+load[5]]+e.data[data[2]:data[2]+data[5]])+bytes(data[6]-data[5])
        def relocated(value):
            offset=value-original_base
            return base+offset if offset<split else dbase+offset-split
        def memory_offset(value):
            if base<=value<base+load[6]: return value-base
            if dbase<=value<dbase+data[6]: return value-dbase+split
            raise AssertionError(f'Pointer outside segments: {value:x}')
        for pos in range(rel[2],rel[2]+rel[5],24):
            offset,zero,value_segment,address_segment,kind,ptr=st.unpack_from('>QHBBIQ',e.data,pos)
            assert zero==0 and address_segment in (0,1) and value_segment in (0,1,255)
            offset+=split if address_segment else 0
            value=ptr+(bases[value_segment] if value_segment!=255 else 0)
            if kind==1: st.pack_into('>I',memory,offset,value)
            elif kind==38: st.pack_into('>Q',memory,offset,value)
            elif kind in (4,5,6):
                half=value if kind==4 else value>>16 if kind==5 else (value+0x8000)>>16
                st.pack_into('>H',memory,offset,half&65535)
            else: raise AssertionError(f'Unsupported relocation {kind}')
        at=load[4]-load[2]
        attrs,major,minor,name,toc,exports,end,imports,imports_end=st.unpack_from('>HBB28sIIIII',memory,at)
        assert name.rstrip(b'\0')==b'ps3_presence'
        assert toc==relocated(original.symbols['.TOC.'])
        assert imports_end-imports==44 and end-exports==28
        imp=st.unpack_from('>BBHHHHH4sIIIIIII',memory,memory_offset(imports))
        assert imp[0]==44 and imp[4]==2
        exp=st.unpack_from('>BBHHHHH4sIII',memory,memory_offset(exports))
        assert exp[0]==28 and exp[3]==0x8000 and exp[4]==2
        ids=st.unpack_from('>II',memory,memory_offset(exp[-2]))
        pointers=st.unpack_from('>II',memory,memory_offset(exp[-1]))
        assert ids==(0xbc9a0086,0xab779874)
        for name,ptr,function in zip(('__presence_start_code','__presence_stop_code'),pointers,('module_start','module_stop')):
            code,rtoc=st.unpack_from('>II',memory,memory_offset(ptr))
            assert code==relocated(original.symbols[name])
            assert rtoc==toc
            # Independently compare with GCC's own OPD64 function descriptor.
            gcc_code,gcc_toc=st.unpack_from('>QQ',memory,original.symbols[function]-original_base)
            assert (code,rtoc)==(gcc_code,gcc_toc)
    wrapped=Path(sprx_path).read_bytes()
    assert wrapped[:4]==b'SCE\0'
    assert st.unpack_from('>H',wrapped,8)[0]==0x8000
    header_size=st.unpack_from('>Q',wrapped,16)[0]
    assert wrapped[header_size:]==e.data
    app=st.unpack_from('>Q',wrapped,0x28)[0]
    assert st.unpack_from('>Q',wrapped,app)[0]==0x1070000052000001
    print('PRX/SELF headers, module exports, lifecycle OPDs, and two relocation bases: PASS')

if __name__=='__main__': verify(*sys.argv[1:])
