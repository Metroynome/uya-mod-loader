# UYA Mod Loader release

Choose `uya-mod-loader-ntscu.zip`, `uya-mod-loader-pal.zip`, or
`uya-mod-loader-ntscj.zip` for your game. The `all` ZIP includes every region.
Each ZIP is standalone; no compilation or libraries are needed to play.

## Installation

1. Copy **both** campaign and multiplayer `SERIAL_CRC.pnach` files for your
   region into PCSX2's cheats directory, then enable **UYA Mod Loader** for both
   game entries. New PCSX2 builds change the active CRC when multiplayer loads
   `I5BOOTN.ELF`. Installing only the campaign file is insufficient.
   Do not combine overlapping loader or Horizon patch cheats.
2. Enable host filesystem access and set the host root to the extracted
   `hostfs` folder. `host0:uya/config/modules.txt` must resolve to its list.
3. Fully restart the game without a savestate. Edit `hostfs/uya/config/modules.txt`
   to enable/disable modules. `uya/modules.txt` is only a fallback when the
   config list cannot be opened.
4. Preserve your `config/*.ini` files when upgrading. Install the loader and
   modules from the same release together; their memory addresses must match.

Mod Menu and Map Loader are enabled for campaign/Local Play where supported.
Tests and examples are not included in release ZIPs. NTSC-J includes campaign
Mod Menu only; its multiplayer hooks and Map Loader are not supported.
Online remains disabled by default because Horizon uses overlapping memory.

In front-end menus, open Mod Menu using L3 + R3. During gameplay, open the
pause menu and choose MOD MENU. Configuration persistence does not require
Mod Menu; see `docs/CONFIG.md` inside the archive.

## Custom maps and optional debugging

Map packs are separate downloads and are not bundled. Put supported map assets
in `hostfs/uya/`, then run:

```sh
python tools/index_maps.py hostfs/uya
```

See `docs/maploader.md` for supported formats and limitations. Python is only
needed to generate a catalog after adding a map pack, not to run the loader.

To enable exception reporting, copy `optional/debug/exceptionhandler.bin` into
`hostfs/uya/mods/debug/exceptionhandler.bin`, creating the directory if needed.
Remove that installed file to disable the handler, then restart the game.

## Building and publishing

From the repository's initialized Docker environment, `make release` builds
all three regions and creates four ZIPs plus `SHA256SUMS.txt` in `release/zips/`.
Ordinary `make` writes development outputs to `build/dist/`. `make release`
refreshes the tracked, ready-to-install files in `release/` and creates ZIPs.
`release/modules.json` is the explicit production-module list; add future
release modules there with their default offline contexts.

The GitHub Actions Release workflow can be run manually to download build
artifacts. Pushing a `v*` tag builds the same ZIPs and attaches them to a draft
GitHub release for review. Release PNACHs, production binaries, and host files are tracked for direct
download. ZIPs remain generated. `make clean` preserves the entire release;
`make clean-release` removes published outputs while keeping this guide and
the module/CRC policies.


## Verified game CRCs

| Region | Boot/campaign | Multiplayer (`I5BOOTN.ELF`) | Serial |
| --- | --- | --- | --- |
| NTSC-U | `45FE0CC4` | `49536F3F` | `SCUS-97353` |
| PAL | `17125698` | `EDE8B391` | `SCES-52456` |
| NTSC-J | `64DC6000` | `68FD292A` (unsupported) | `SCPS-15084` |

The five supported filenames are ready to copy; campaign and multiplayer
aliases contain identical guarded regional loader code. NTSC-J has only a
campaign cheat because its multiplayer hooks are unverified. Network setup
and bundled demo ELF CRCs are deliberately not targeted.

These values were calculated from the retail ISOs using PCSX2's
[ELF CRC algorithm](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Elfheader.cpp).
`release/crcs.json` records their ELF names and support status. To reproduce:

```sh
python tools/verify_release_crcs.py ntscu "path/to/UYA.iso"
```

Use `pal` or `ntscj` for those discs. No disc data is needed for normal release
builds or included in the release. For older PCSX2 versions that only recognize
CRC-only names, rename each matching file to `<CRC>.pnach`; keep both CRCs for
NTSC-U/PAL. Other disc revisions require separate CRC/address verification.
