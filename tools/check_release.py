#!/usr/bin/env python3
"""Validate the actual release ZIPs before uploading them."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REGIONS = {'ntscu': 1, 'pal': 2, 'ntscj': 3}
TEST_MODULES = {'hello', 'menu-example', 'campaign-test', 'multiplayer-test'}


def check():
    folder = ROOT / 'release/zips'
    crc_table = json.loads((ROOT / 'release/crcs.json').read_text())
    sums = dict(line.split('  ', 1)[::-1] for line in (folder / 'SHA256SUMS.txt').read_text().splitlines())
    for label in [*REGIONS, 'all']:
        path = folder / f'uya-mod-loader-{label}.zip'
        assert hashlib.sha256(path.read_bytes()).hexdigest() == sums[path.name], path
        regions = set(REGIONS) if label == 'all' else {label}
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None, path
            names = archive.namelist()
            assert len(names) == len(set(names)), 'duplicate ZIP entries'
            assert 'README.md' in names
            assert not any(n.startswith(('hostfs/', 'docs/', 'optional/', 'tools/')) for n in names)
            for script in ('horizon_unix_update_cmaps.sh', 'horizon_windows_update_cmaps.ps1'):
                assert archive.read(script) == (ROOT / 'tools/horizon' / script).read_bytes()
            assert not any(n.endswith('.ini') or n.endswith('.wad') for n in names), 'local user files leaked'
            assert 'uya/modules.txt' not in names
            assert 'tools/index_maps.py' not in names
            if regions & {'ntscu', 'pal'}:
                assert archive.read('uya/mods/utils/mapscan.irx')[:4] == b'\x7fELF'
            manifest = archive.read('uya/config/modules.txt').decode()
            enabled = set()
            for line in manifest.splitlines():
                if not line or line.startswith('#'): continue
                name, contexts = line.split()
                module, region = name.rsplit('.', 1)
                assert region in regions and module not in TEST_MODULES
                assert contexts in ('campaign', 'local', 'campaign,local')
                filename = f'uya/mods/{name}.bin'
                data = archive.read(filename)
                header = struct.unpack('<10I', data[:40])
                assert header[0] == 0x4d415955 and header[2] == REGIONS[region]
                assert len(data) == header[4] + 40
                enabled.add(filename)
            modules = {n for n in names if n.startswith('uya/mods/') and n.count('/') == 2 and n.endswith('.bin')}
            assert modules == enabled, 'unlisted module or missing binary'
            for region in regions:
                for elf in crc_table[region]['elfs']:
                    if elf['supported']:
                        patch = archive.read(f"{crc_table[region]['serial']}_{elf['crc']}.pnach")
                        assert patch.startswith(b'[UYA Mod Loader]\nauthor=Metroynome\n')
                        assert b'patch=1,EE,' in patch
                assert f'uya/mods/modmenu.{region}.bin' in modules
                if region != 'ntscj':
                    assert f'uya/mods/maploader.{region}.bin' in modules
            assert not any('maploader.ntscj' in n for n in names)
            patches = {n for n in names if n.endswith('.pnach')}
            expected = {f"{crc_table[r]['serial']}_{e['crc']}.pnach"
                        for r in regions for e in crc_table[r]['elfs'] if e['supported']}
            assert patches == expected
            for region in regions:
                copies = [archive.read(f"{crc_table[region]['serial']}_{e['crc']}.pnach")
                          for e in crc_table[region]['elfs'] if e['supported']]
                assert all(p == copies[0] for p in copies), 'ELF aliases differ'
            if label == 'all':
                for name in names:
                    assert archive.read(name) == (ROOT / 'release' / name).read_bytes()
        print(f'Validated {path.name}')


if __name__ == '__main__':
    check()
