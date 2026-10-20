#!/usr/bin/env python3
"""Legacy v1 catalog utility; current Map Loader builds its v2 cache in-game.

Run against the actual host0:uya directory. Map payloads stay in place.
"""
import argparse
from pathlib import Path
import struct

MAGIC = 0x3150414D
LIMIT = 96

def fixed(text, size):
    data = text.encode("ascii")
    if not data or len(data) >= size or text.startswith(".") or any(
        ord(c) < 32 or c in '/\\:"*?' for c in text
    ):
        raise ValueError("Invalid or overlong map name: " + repr(text))
    return data.ljust(size, b"\0")

def catalog(folder, region):
    entries = []
    suffix = ".pal" if region == "pal" else ""
    for file in sorted(folder.glob("*.version"), key=lambda p: p.name.lower()):
        data = file.read_bytes()
        try:
            if len(data) < 48:
                raise ValueError("short .version file")
            version, base, forced = struct.unpack_from("<iih", data)
            name = data[16:48].split(b"\0", 1)[0].decode("ascii")
            if version < 0 or base not in range(40, 50):
                raise ValueError("requires a retail multiplayer base ID 40..49")
            if forced:
                raise ValueError("requires a Horizon custom game mode")
            if (folder / (file.stem + suffix + ".code")).exists():
                raise ValueError("requires a custom-code ABI not provided by this loader")
            sizes = []
            for ext, maximum in [("wad", 0x1000000), ("world", 0x400000),
                                 ("sound", 0x400000), ("map", 0x10800)]:
                part = folder / (file.stem + suffix + "." + ext)
                size = part.stat().st_size if part.exists() else 0
                if size > maximum:
                    raise ValueError(ext + " exceeds the supported buffer limit")
                sizes.append(size)
            if not any(sizes[:2]):
                continue
            entries.append((name.lower(), fixed(file.stem, 64) + fixed(name, 32) +
                            struct.pack("<6I", version, base, *sizes)))
        except (ValueError, UnicodeError) as exc:
            print(f"{region}: skipping {file.name}: {exc}")
    if len(entries) > LIMIT:
        raise ValueError(f"{region}: {len(entries)} maps exceeds catalog limit {LIMIT}")
    entries.sort(key=lambda entry: entry[0])
    return struct.pack("<2I", MAGIC, len(entries)) + b"".join(e[1] for e in entries)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "hostfs/uya")
    args = parser.parse_args()
    if not args.folder.is_dir():
        parser.error("Expected the host0:uya directory")
    for region in ("ntscu", "pal"):
        data = catalog(args.folder, region)
        output = args.folder / ("maps." + region + ".idx")
        output.write_bytes(data)
        print(f"{output}: {(len(data)-8)//120} maps")

if __name__ == "__main__":
    main()
