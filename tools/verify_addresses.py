#!/usr/bin/env python3
"""Check documented address anchors against the user's extracted RAC3 files."""
import argparse
from pathlib import Path
import struct

CHECKS = [(0x121760,0x128c70,0x27bdff40),
          (0x123430,0x12aa38,0x27bdfeb0),
          (0x1236c0,0x12acc8,0x27bdff60),
          (0x123840,0x12b080,0x27bdff20),
          (0x11f0a0,0x1264e0,0x24030064)]

def word(data, offset): return struct.unpack_from('<I',data,offset)[0]
def verify(data, base, multiplayer):
    for sp, mp, expected in CHECKS:
        address=mp if multiplayer else sp
        actual=word(data,address-base)
        assert actual==expected, f'{address:08x}: {actual:08x} != {expected:08x}'
    address = 0x139e94 if multiplayer else 0x12c994
    assert word(data,address-4-base)==0xdfb00000
    assert word(data,address+4-base)==0x27bd0020
    original = word(data,address-base)
    # Campaign EE snapshots already contain the printf template's jump.
    assert original == 0x03e00008 or (not multiplayer and original == 0x0803c000), hex(original)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rac3',type=Path)
    args=parser.parse_args()
    files=list((args.rac3/'boot/core1').glob('*.bin'))
    assert len(files)==3, 'Expected three regional boot/core1 files'
    for file in files:
        verify(file.read_bytes(),0x116f80,False)
        print('Verified campaign:',file.name)
    verify((args.rac3/'eemem/ntsc.39.PreLoad_MultiplayerMenu.bin').read_bytes(),0,True)
    for file in sorted((args.rac3/'eemem/campaign').glob('*.bin')):
        data=file.read_bytes()
        verify(data,0,False)
        assert word(data,0x1d545c)==int(file.name.split('.')[1])
        print('Verified campaign SDK and level ID:',file.name)
    for file in sorted((args.rac3/'eemem/multiplayer').glob('*.bin')):
        data=file.read_bytes()
        verify(data,0,True)
        address=0x1f83a8 if file.name.startswith('pal.') else 0x1f8528
        assert word(data,address)==int(file.name.split('.')[1])
        print('Verified multiplayer SDK and level ID:',file.name)
    # Same Annihilation Nation branch in all three extracted regional overlays.
    # Match its invariant instructions, then decode the preceding LUI/LW pair.
    reference=(args.rac3/'eemem/campaign/ntscu.07.AnniNation.bin').read_bytes()
    signature=reference[0x32ba40:0x32ba5c]
    for file in sorted((args.rac3/'code5').glob('rac3.level7.*')):
        data=file.read_bytes()
        start=0
        addresses=[]
        while True:
            offset=data.find(signature,start)
            if offset<0: break
            assert offset>=8
            hi,lo=struct.unpack_from('<2I',data,offset-8)
            assert hi & 0xffff0000 == 0x3c030000 and lo & 0xffff0000 == 0x8c630000
            low=lo & 0xffff
            if low & 0x8000: low-=0x10000
            addresses.append(((hi & 0xffff)<<16)+low)
            start=offset+1
        expected=0x1de75c if '.ntscj.' in file.name else 0x1d545c
        # Two compiled copies of this check, both referencing the same global.
        assert addresses==[expected,expected]
        print('Verified regional campaign global:',file.name,hex(expected))
    print('NTSC-J multiplayer remains unverified')
