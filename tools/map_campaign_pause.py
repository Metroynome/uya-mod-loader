#!/usr/bin/env python3
"""Verify campaign pause option tables and generate regional anchors."""
import argparse,json,struct
from pathlib import Path
from map_menu_renderers import fnv

def generate(root):
    defs=json.loads((root.parent/'rac-defs.json').read_text())['rac']['3']['levels']
    level=next(x for x in defs if x['level']==3)
    def image(l,r):
        m=l['region'][r];v=m['defaultVersion'];i=m['version'][v]
        name='rac3.level%d.%s.v%s.%s.code.0005.bin'%(l['level'],r,v.replace('.',''),i['buildType'])
        return (root/'code5'/name).read_bytes(),int(i['code']['5']['offset'],16),(root/'code1'/name.replace('0005','0001')).read_bytes(),int(i['code']['1']['offset'],16)
    ref,base,_,_=image(level,'ntscu')
    sig=struct.unpack_from('<64I',ref,0x4693d0-base)
    msg=struct.unpack_from('<16I',ref,0x405420-base)
    jp,jbase,_,_=image(next(x for x in defs if x['level']==1),'ntscj')
    jp_sig=struct.unpack_from('<64I',jp,0x4866b8-jbase)
    def find(data,words):
        masks=[0xfc000000 if w>>26 in (2,3) else 0xffff0000 if (w>>26 in (9,15) or w>>26>=32) else 0xffffffff for w in words]
        hits=[];start=0
        while True:
            p=data.find(struct.pack('<I',words[0]),start)
            if p<0:break
            start=p+4
            if p%4 or p+len(words)*4>len(data):continue
            actual=struct.unpack_from('<%dI'%len(words),data,p)
            if all(a&m==b&m for a,b,m in zip(actual,words,masks)):hits.append(p)
        if len(hits)!=1:raise ValueError(('ambiguous',len(words),hits))
        return hits[0]
    out=['/* Generated campaign pause anchors, verified against retail overlays. */',
         'struct CampaignPause { int level; unsigned int setup,message,parent,push,setupHash,messageHash; };']
    for region,num in [('ntscu',1),('pal',2),('ntscj',3)]:
        out+=['#if UYA_MODULE_REGION == %d'%num,'static const struct CampaignPause campaignPauses[] = {']
        count=0
        for l in defs:
            if not 0<=l['level']<39 or region not in l['region']:continue
            data,base,lit,lb=image(l,region)
            try:s=find(data,jp_sig if num==3 else sig);m=find(data,msg)
            except ValueError as e:raise ValueError((region,l['level'],e))
            shift=-4 if num==3 else 0
            w=lambda o:struct.unpack_from('<I',data,s+o+shift)[0]
            for hi,lo,n in [(0x78,0x80,9),(0xb8,0xc0,9),(0xd4,0xdc,8)]:
                addr=((w(hi)&65535)<<16)+struct.unpack('<h',struct.pack('<H',w(lo)&65535))[0]
                rows=struct.unpack_from('<%dI'%(n*5),lit,addr-lb)
                assert all(rows[i*5+2]==1 for i in range(n))
                assert rows[-4]==0x522 and rows[-2]>base
            # The normal Options callback calls parent getter then pushes it.
            a=((w(0xd4)&65535)<<16)+struct.unpack('<h',struct.pack('<H',w(0xdc)&65535))[0]
            assert struct.unpack_from('<I',lit,a-lb+5*20+4)[0]==0x107
            # Back/Start returns to the gameplay UI and closes the pause screen.
            close_sig=[0x30820910,0x10400008,0x8f82baac,0x0c000000,0,0x0c000000,0x0040202d,0x0c000000,0x0240202d]
            close=s+find(data[s:s+0x1800],close_sig)
            parent=(struct.unpack_from('<I',data,close+12)[0]&0x3ffffff)*4
            push=(struct.unpack_from('<I',data,close+20)[0]&0x3ffffff)*4
            out.append('    {%d,0x%x,0x%x,0x%x,0x%x,0x%xU,0x%xU},'%(l['level'],base+s,base+m,parent,push,fnv(data[s:s+0x100]),fnv(data[m:m+64])))
            count+=1
        out+=['};','#endif'];print(region,count,'verified campaign pause tables')
    return '\n'.join(out)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rac3',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    text=generate(a.rac3);dest=Path(__file__).resolve().parents[1]/'modules/modmenu/campaign-pause.h'
    if a.check:assert dest.read_text()==text
    else:dest.write_text(text)

