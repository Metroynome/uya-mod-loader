#!/usr/bin/env python3
"""Verify recorded CRCs from a retail ISO; never extracts/distributes game bytes.
PCSX2 ElfObject::GetCRC XORs little-endian 32-bit words of the entire ELF.
https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Elfheader.cpp
"""
import argparse
import functools
import json
import operator
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def verify(region, iso):
    entry = json.loads((ROOT / 'release/crcs.json').read_text())[region]
    with iso.open('rb') as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        if pvd[:7] != b'\x01CD001\x01': raise ValueError('Expected an ISO9660 image')
        record = pvd[156:190]
        f.seek(struct.unpack_from('<I', record, 2)[0] * 2048)
        directory = f.read(struct.unpack_from('<I', record, 10)[0])
        files = {}
        offset = 0
        while offset < len(directory):
            size = directory[offset]
            if not size:
                offset = (offset // 2048 + 1) * 2048
                continue
            row = directory[offset:offset + size]
            if size < 34: raise ValueError('Malformed directory record')
            offset += size
            name = row[33:33 + row[32]].decode('ascii')
            files[name] = (struct.unpack_from('<I', row, 2)[0], struct.unpack_from('<I', row, 10)[0])
        for elf in entry['elfs']:
            sector, size = files[elf['path']]
            if size > 16 * 1024 * 1024: raise ValueError('Unexpected ELF size')
            f.seek(sector * 2048)
            data = f.read(size)
            if len(data) != size or data[:4] != b'\x7fELF': raise ValueError('Invalid ELF')
            crc = functools.reduce(operator.xor, (v[0] for v in struct.iter_unpack('<I', data[:size // 4 * 4])), 0)
            if f'{crc:08X}' != elf['crc']: raise ValueError(f"CRC mismatch: {elf['path']} {crc:08X}")
            print(f"{region}: {elf['path']} = {crc:08X} ({elf['mode']}, supported={elf['supported']})")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('region', choices=['ntscu', 'pal', 'ntscj'])
    parser.add_argument('iso', type=Path)
    args = parser.parse_args()
    verify(args.region, args.iso)
