#!/usr/bin/env python3
"""Package an allowlisted release; never include local settings or map packs."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile
import build

ROOT = Path(__file__).resolve().parents[1]


def contents(regions):
    policy = json.loads((ROOT / 'release/modules.json').read_text())
    entries = {entry[2][0]: entry for entry in build.configs()}
    unknown = policy.keys() - entries.keys()
    if unknown:
        raise ValueError('Unknown release modules: ' + ', '.join(sorted(unknown)))
    result = {
        'README.md': (ROOT / 'release/README.md').read_bytes(),
        'uya/maploader-online.txt': b'0\n',
    }
    for name in ('horizon_unix_update_cmaps.sh', 'horizon_windows_update_cmaps.ps1'):
        result[name] = (ROOT / 'tools/horizon' / name).read_bytes()
    crc_table = json.loads((ROOT / 'release/crcs.json').read_text())
    if any(r in ('ntscu', 'pal') for r in regions):
        result['uya/mods/utils/mapscan.irx'] = (ROOT / 'hostfs/uya/mods/utils/mapscan.irx').read_bytes()
    lines = ['# Release defaults: no test/example modules.',
             '# Restart the game after editing. See README.md.']
    for region in regions:
        patch = (ROOT / f'build/dist/mod-loader.{region}.pnach').read_bytes()
        titled = b'[UYA Mod Loader]\nauthor=Metroynome\n' + patch
        for elf in crc_table[region]['elfs']:
            if elf['supported']:
                result[f"{crc_table[region]['serial']}_{elf['crc']}.pnach"] = titled
        layout = {item['name']: item for item in json.loads((ROOT / f'build/{region}/layout.json').read_text())}
        for name, contexts in policy.items():
            if contexts not in ('campaign', 'local', 'campaign,local'):
                raise ValueError(f'{name}: release defaults must stay offline')
            if region not in entries[name][1]:
                continue
            filename = f'{name}.{region}.bin'
            data = (ROOT / 'hostfs/uya/mods' / filename).read_bytes()
            header = struct.unpack('<10I', data[:40])
            item = layout[name]
            if (header[0] != 0x4d415955 or header[2] != build.REGIONS[region]
                    or header[3] != int(item['address'], 16) or header[5] != item['bytes']
                    or len(data) != 40 + header[4]):
                raise ValueError(f'{filename}: rebuild all modules before packaging')
            result[f'uya/mods/{filename}'] = data
            # Explicit suffixes make the combined list skip other regions cleanly.
            context = 'campaign' if region == 'ntscj' else contexts
            lines.append(f'{name}.{region} {context}')
    result['uya/config/modules.txt'] = ('\n'.join(lines) + '\n').encode()
    debug = ROOT / 'hostfs/uya/mods/debug/exceptionhandler.bin'
    if debug.is_file():
        result['uya/mods/utils/exceptionhandler.bin'] = debug.read_bytes()
    return result


def package():
    destination = ROOT / 'release/zips'
    destination.mkdir(parents=True, exist_ok=True)
    sums = []
    # Validate every bundle before writing any archives.
    bundles = [(region, contents([region])) for region in build.REGIONS]
    bundles.append(('all', contents(list(build.REGIONS))))
    # Materialize the combined bundle for direct repository downloads.
    published = ROOT / 'release'
    inventory = published / 'files.json'
    combined = bundles[-1][1]
    if inventory.is_file():
        for old in json.loads(inventory.read_text()):
            path = (published / old).resolve()
            if published.resolve() not in path.parents:
                raise ValueError('Invalid release inventory path')
            if old not in combined and path.is_file(): path.unlink()
    for name, data in combined.items():
        target = published / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    inventory.write_text(json.dumps(sorted(combined), indent=2) + '\n')
    for region, files in bundles:
        path = destination / f'uya-mod-loader-{region}.zip'
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o100755 if name.endswith('.sh') else 0o100644) << 16
                archive.writestr(info, data)
        sums.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}')
        print(f'Packaged {path.name}: {len(files)} files')
    (destination / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n')


if __name__ == '__main__':
    package()
