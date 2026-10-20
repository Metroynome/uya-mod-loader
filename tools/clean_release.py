#!/usr/bin/env python3
"""Remove published generated outputs, retaining release instructions/policy."""
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def clean():
    root = (ROOT / 'release').resolve()
    inventory = root / 'files.json'
    if inventory.is_file():
        for name in json.loads(inventory.read_text()):
            path = (root / name).resolve()
            if root not in path.parents:
                raise ValueError('Invalid release inventory path')
            if name != 'README.md' and path.is_file(): path.unlink()
        inventory.unlink()
    zips = (root / 'zips').resolve()
    if root not in zips.parents: raise ValueError('Invalid ZIP directory')
    if zips.is_dir(): shutil.rmtree(zips)


if __name__ == '__main__':
    clean()
