#!/usr/bin/env python3
"""Restore the stripped campaign stdio writer using verified resident SDK peers.
Requires the user's retail extraction. No game allocator or heap edits.
"""
import argparse, struct
from pathlib import Path

def generate(root):
    mp=(root/'eemem/multiplayer/ntsc.39.Lobby.bin').read_bytes()
    read=struct.unpack_from('<156I',mp,0x12b080)
    source=struct.unpack_from('<176I',mp,0x12b2f0)
    assert source[0]==0x27bdff20 and source[-2]==0x03e00008
    for p in (root/'eemem/multiplayer').glob('*.bin'):
        actual=struct.unpack_from('<176I',p.read_bytes(),0x12b2f0)
        for x,y in zip(source,actual):
            if x!=y:assert x&0xffff0000==y&0xffff0000 and x>>26 in (9,35,43) and (x-y)&65535==0x180,(p,hex(x),hex(y))
    out=['/* Generated retail stdio writer. Campaign retains the descriptor/RPC',
         ' * support but strips write(). Branch offsets stay intact; resident',
         ' * data and function calls below are relocated per region. */',
         '.set noreorder','.text','.align 4','.globl campaignConfigWrite','campaignConfigWrite:']
    for region,num in [('ntscu',1),('pal',2),('ntscj',3)]:
        p=next((root/'boot/core1').glob('*.'+region+'.*'))
        sp=struct.unpack_from('<156I',p.read_bytes(),0x123840-0x116f80)
        calls={};high={};low={}
        for a,b in zip(read,sp):
            if a==b:continue
            assert a>>26==b>>26
            if a>>26==3: calls[a]=b
            else:
                assert a&0xffff0000==b&0xffff0000
                mapping=high if a>>26==15 else low
                if a&65535 in mapping:assert mapping[a&65535]==b&65535
                mapping[a&65535]=b&65535
        # Semaphore-name strings are adjacent in both SDKs (+24).
        low[0x4560]=low[0x4548]+24
        out.append('#if UYA_MODULE_REGION == %d'%num)
        for w in source:
            op=w>>26
            if op==3:
                assert w in calls,hex(w)
                w=calls[w]
            elif op==15 and w&65535 in high:w=(w&0xffff0000)|high[w&65535]
            elif op in (9,35,43) and w&65535 in low:w=(w&0xffff0000)|low[w&65535]
            out.append('    .word 0x%08x'%w)
        out.append('#endif')
    return '\n'.join(out)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rac3',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    dest=Path(__file__).resolve().parents[1]/'campaign-write.S';text=generate(a.rac3)
    if a.check:assert dest.read_text()==text
    else:dest.write_text(text)
