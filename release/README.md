# UYA Mod Loader

Extract the ZIP for your region, or use the `all` ZIP for every supported region.
No build tools or libraries are needed to play.

## Install

1. Copy your region's `.pnach` files into PCSX2's cheats folder. NTSC-U and PAL
   need **both** campaign and multiplayer files because they use different CRCs.
   Enable **UYA Mod Loader** for both game entries.
2. Enable host filesystem access. Set the host root to the extracted folder
   containing `uya/`, or copy `uya/` into your existing host root.
3. Edit `uya/config/modules.txt` to enable modules, then fully restart the game
   without a savestate. `uya/modules.txt` is a fallback location.
4. Preserve your `uya/config/*.ini` settings when upgrading. Replace the loader
   and module binaries together with files from the same release.

Release defaults include Mod Menu, BIOS Warning, and Map Loader where supported,
without tests/examples. NTSC-J supports campaign only. Forge is opt-in and must
be built/copied separately. When upgrading to v2, replace older modules with
compatible version-4 binaries.

Online coexistence with Horizon is not verified. The tested downloaded patch
still used the loader's address for a network callback pointer and crashed.
Updated Horizon networking binaries are required before retesting; keeping
modules disabled online does not resolve that address conflict.

Use L3 + R3 in front-end menus; during gameplay choose MOD MENU from the pause menu.

## Download custom maps

The two included download scripts are copies of Horizon's updater scripts.
Run them **from this folder, alongside `uya/`**, or copy them to your existing
host root and run them there. They create/update the `uya/` map directory.

- Windows: `powershell -File .\horizon_windows_update_cmaps.ps1`
- Bash: `bash ./horizon_unix_update_cmaps.sh` (requires wget, hexdump and realpath).

Choose the menu option for UYA NTSC or R&C3 PAL. Menu numbers differ between
scripts. Follow any script-update prompt and rerun if requested. Map packs are
separate downloads, not included in the ZIP.

`uya/mods/utils/mapscan.irx` is the required directory helper for Map Loader.
Enter Local Play staging for the first scan. A progress popup captures input
until it finishes. The catalog stays in memory across staging visits and games;
no `.idx` files are used and new files are not detected automatically. Use
Forge → Refresh Custom Maps or a PINE refresh command after changing maps.
Without Forge, restart the game to rescan. Do not change assets while scanning
or loading a game. Map packs requiring Horizon custom code/game modes are unsupported.

`uya/mods/utils/` is also available for other mods' supporting files. Keep
module binaries directly in `uya/mods/` and settings in `uya/config/`.

## Optional exception handler

To enable it, copy `uya/mods/utils/exceptionhandler.bin` into
`uya/mods/debug/exceptionhandler.bin`, creating the debug folder if needed.
Remove the copy from `debug/` to disable it, then restart the game.

## Expanded RAM

The loader can place modules in PCSX2's 128 MiB RAM when the BIOS supports it.
Older BIOS versions may crash when entering multiplayer even if modules fall
back to normal memory. If BIOS Warning appears, disable expanded RAM or use a
compatible BIOS, then restart. Cross or Triangle dismisses the warning; it does
not fix the setting.

## Regions and documentation

| Region | Campaign CRC | Multiplayer CRC |
| --- | --- | --- |
| NTSC-U (SCUS-97353) | 45FE0CC4 | 49536F3F |
| PAL (SCES-52456) | 17125698 | EDE8B391 |
| NTSC-J (SCPS-15084) | 64DC6000 | Unsupported |

Older PCSX2 builds may need each file renamed to `<CRC>.pnach`.
Full [configuration documentation](https://github.com/Metroynome/uya-mod-loader/blob/master/CONFIG.md),
[map limitations](https://github.com/Metroynome/uya-mod-loader/blob/master/modules/maploader/README.md),
and build instructions are in the repository.
