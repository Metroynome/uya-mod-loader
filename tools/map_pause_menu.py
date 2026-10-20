#!/usr/bin/env python3
"""Generate verified multiplayer pause entry / message lookup addresses."""
import argparse,json,struct
from pathlib import Path
from map_menu_renderers import fnv

def generate(root):
    ref=next((root/'eemem/multiplayer').glob('ntsc.40.*')).read_bytes()
    ret=struct.unpack_from('<22I',ref,0x4b3be0)
    msg=struct.unpack_from('<16I',ref,0x466108)
    def matches(data,signature,masks):
        result=[];start=0
        while True:
            pos=data.find(struct.pack('<I',signature[0]),start)
            if pos<0: return result
            start=pos+4
            if pos%4 or pos+len(signature)*4>len(data): continue
            words=struct.unpack_from('<%dI'%len(signature),data,pos)
            if all(a&m==b&m for a,b,m in zip(words,signature,masks)):result.append(pos)
    rm=[0xfc000000 if w>>26==3 else 0xffffffff for w in ret]
    mm=[0xfc000000 if w>>26==3 else 0xffff0000 if i in (5,8,9) else 0xffffffff for i,w in enumerate(msg)]
    defs=json.loads((root.parent/'rac-defs.json').read_text())['rac']['3']['levels']
    out=['/* Generated from verified retail pause entries; do not guess offsets. */',
         'struct PauseEntry { int level; unsigned int entry, callback, message, callbackHash, messageHash; };']
    for region,num in [('ntscu',1),('pal',2)]:
        out+=['#if UYA_MODULE_REGION == %d'%num,'static const struct PauseEntry pauseEntries[] = {']
        count=0
        for level in defs:
            n=level['level']
            if n<40 or n>55:continue
            meta=level['region'][region];v=meta['defaultVersion'];info=meta['version'][v]
            name='rac3.level%d.%s.v%s.%s.code.0005.bin'%(n,region,v.replace('.',''),info['buildType'])
            data=(root/'code5'/name).read_bytes();base=int(info['code']['5']['offset'],16)
            rs=matches(data,ret,rm);ms=matches(data,msg,mm)
            if len(rs)!=1 or len(ms)!=1:raise ValueError((region,n,rs,ms))
            r=rs[0];m=ms[0]
            literals=(root/'code1'/name.replace('0005','0001')).read_bytes()
            sig=struct.pack('<4I',0x2cc,1,base+r,0xd000d)
            pos=literals.find(sig)
            if pos<0 or literals.count(sig)!=1:raise ValueError(('entry',region,n))
            entry=int(info['code']['1']['offset'],16)+pos
            out.append('    {%d,0x%x,0x%x,0x%x,0x%xU,0x%xU},'%(n,entry,base+r,base+m,fnv(data[r:r+84]),fnv(data[m:m+64])))
            count+=1
        out+=['};','#endif'];print(region,count,'verified pause entries')
    return '\n'.join(out)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rac3',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    text=generate(a.rac3);dest=Path(__file__).resolve().parents[1]/'modules/modmenu/pause.h'
    if a.check:assert dest.read_text()==text
    else:dest.write_text(text)

