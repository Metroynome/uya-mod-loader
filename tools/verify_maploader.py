#!/usr/bin/env python3
"""Verify maploader's NTSC-U/PAL anchors against extracted retail files."""
import argparse
import json
from pathlib import Path
import struct

def word(data, address):
    return struct.unpack_from("<I", data, address)[0]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rac3", type=Path)
    root = parser.parse_args().rac3
    levels = json.loads((root.parent / "rac-defs.json").read_text())["rac"]["3"]["levels"]
    tables = {
        "ntscu": ([0x19507c, 0x5a2560, 0x5a2408, 0x5bf36c, 0x5a258c, 0x5a2594],
                  [0x194fc8, 0x5c09f8, 0x194b28, 0x193978, 0x5eeff0, 0x5bf328],
                  [0x4931b4,0x49520c,0x48ab24,0x488274,0x48768c,0x484ea4,0x48505c,0x48789c,0x486bdc,0x48651c],
                  [0x4a41b4,0x4a620c,0x49bb24,0x499274,0x49868c,0x495ea4],0x194a60),
        "pal": ([0x194f8c, 0x5a41c8, 0x5a4070, 0x5c1354, 0x5a41f4, 0x5a41fc],
                [0x194ed8, 0x5c2a40, 0x194a38, 0x193888, 0x5f16f0, 0x5c1310],
                [0x4951a4,0x4972bc,0x48cb94,0x48a264,0x48967c,0x486f14,0x486f0c,0x48978c,0x488a8c,0x48840c],
                [0x4a61a4,0x4a82bc,0x49db94,0x49b264,0x49a67c,0x497f14],0x194970),
    }
    for region, (hooks, targets, radar, local, cdread) in tables.items():
        prefix = "ntsc" if region == "ntscu" else "pal"
        lobby = (root / f"eemem/multiplayer/{prefix}.39.Lobby.bin").read_bytes()
        for address, target in zip(hooks, targets):
            assert word(lobby, address) == 0x0c000000 | (target >> 2), hex(address)
        name_hook = 0x675dc0 if region == "ntscu" else 0x6787a8
        assert lobby[name_hook-8:name_hook+8] == bytes.fromhex("4000bedf4800bfdf0800e0035000bd27")
        message = 0x599618 if region == "ntscu" else 0x59a970
        call = 0x0c16656a if region == "ntscu" else 0x0c166a40
        load = 0x8c42bdc0 if region == "ntscu" else 0x8c42bcc0
        assert struct.unpack_from("<16I", lobby, message) == (
            0x27bdfff0,0xffbf0000,call,0,0x0040182d,0x2782aaf8,0x04600005,
            0x00031900,0x3c020025,load,0x00621821,0x8c620000,
            0xdfbf0000,0x03e00008,0x27bd0010,0)
        # seek(fd, offset, whence): argument moves and resident service command 4.
        assert word(lobby, 0x12ae40) == 0x27bdff40
        assert word(lobby, 0x12ae4c) == 0x00c0a82d
        assert word(lobby, 0x12ae54) == 0x00a0982d
        assert word(lobby, 0x12afc8) == 0x24050004
        reference = None
        for level, address in enumerate(radar, 40):
            file = next((root / "eemem/multiplayer").glob(f"{prefix}.{level}.*.bin"))
            data = file.read_bytes()
            assert word(data, address) == 0x0c000000 | (cdread >> 2)
            if level == 40:
                reference = data[address-20:address+20]
        for level, address in enumerate(local, 50):
            meta = next(l for l in levels if l["level"] == level)["region"][region]
            version = meta["defaultVersion"]
            info = meta["version"][version]
            file = root / "code5" / (
                f"rac3.level{level}.{region}.v{version.replace('.', '')}.{info['buildType']}.code.0005.bin")
            data = file.read_bytes()
            offset = address - int(info["code"]["5"]["offset"], 16)
            assert data.count(reference) == 1 and data[offset-20:offset+20] == reference
        print(f"{region}: loading/seek hooks and 10 online + 6 local radar hooks verified")

if __name__ == "__main__":
    main()
