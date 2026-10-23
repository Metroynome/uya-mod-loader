# UYA Mod Loader

A standalone module loader for Ratchet & Clank 3 / Up Your Arsenal, supporting
campaign and Local Play. Based on the SM module loader, without heap-size edits.
Modules load from PCSX2's host filesystem and receive addresses automatically.

## Install in PCSX2

1. Extract the release ZIP for your region, or use the `all` ZIP.
2. Copy the PNACH files into PCSX2's cheats folder and enable the cheats.
   **NTSC-U and PAL need both campaign and multiplayer PNACHs** because they use
   different CRCs. See the [CRC table](release/README.md#regions-and-documentation).
3. Enable host filesystem access and set its root to the folder containing `uya/`.
4. Edit `uya/config/modules.txt`, then fully restart emulation without a savestate.

Preserve `uya/config/*.ini` when upgrading. Version 2 requires matching loader
and rebuilt version-4 module binaries; older binary formats are rejected.

For source builds, use `build/dist/mod-loader.<region>.pnach` with your game's
CRC and set the host root to `build/dist/hostfs`. Rebuilding replaces the packaged
module list with the source copy from `hostfs/uya/config/modules.txt`.

## Enable modules

```text
uya/
  config/
    modules.txt
    <mod-name>.<region>.ini
  mods/
    modmenu.ntscu.bin
    maploader.ntscu.bin
    utils/mapscan.irx
    debug/exceptionhandler.bin   # optional
```

Edit `uya/config/modules.txt` (or `hostfs/uya/config/modules.txt` when building):

```text
modmenu       campaign,local
bios-warning  campaign,local
maploader     local
# Optional: build/copy Forge first, then enable it.
forge         disabled
```

| Context | Where the module runs |
| --- | --- |
| `campaign` | Campaign and its boot menu |
| `local` | Multiplayer menus, Local Play staging, and offline games |
| `online` | From post-DNAS profile selection, before login |
| `all` | All supported contexts |
| `disabled` | Not loaded |

Join contexts with commas. Legacy values `0` (disabled), `1` (campaign),
`2` (local and online multiplayer), and `3` (all) also work. `#` and `//` start
comments. Configuration cannot expand a module's compiled library/region support.

The loader adds the current region and `.bin` automatically. Explicit names
such as `maploader.pal` or `maploader.pal.bin` restrict an entry to that region.
Use lowercase names and either automatic or regional entries for a module;
if entries resolve to the same binary, the first successful load wins.

`uya/modules.txt` is a fallback when `uya/config/modules.txt` cannot be opened.
Lists are not merged. The limit is **eight module images** and 2047 bytes of list
text. Disabled entries consume no module slots. Restart after changing the list
or replacing binaries.

## Included modules

- **Mod Menu:** extensible tabs, toggles, values, and actions. Use **L3 + R3** in
  front-end menus; during gameplay, choose **MOD MENU** from the pause menu.
  L1/R1 change tabs, the D-pad navigates, Cross selects, and Triangle closes.
- **Map Loader:** Horizon-format custom maps plus retail maps in one alphabetical
  list. Scans on the first Local Play staging visit and retains the catalog in
  memory. Use **Refresh Custom Maps** after adding/removing maps; no `.idx` cache
  is required. Keep map packs in `uya/` and the helper in `uya/mods/utils/`.
  Campaign map replacement is not implemented.
- **Forge:** reload the current map or hop to another retail/custom map without
  creating a new game. Enables the in-game Custom Maps list and offers PINE
  commands for refresh, selection, reload, and **Start Game** from staging.
  Supports NTSC-U/PAL Local Play bases 40–49; split-screen and NTSC-J hopping
  are disabled. Editor-side Forge v2 integration remains separate.
- **BIOS Warning:** optional Mod Menu popup for incompatible expanded-RAM BIOS
  configurations. Cross or Triangle dismisses it.
- **Examples/tests:** `hello`, `menu-example`, `campaign-test`, and
  `multiplayer-test` are available in source, excluded from release packages.

Release defaults are controlled by [release/modules.txt](release/modules.txt).
Forge is currently opt-in and must be built/copied separately.

See the [Mod Menu API](modules/modmenu/README.md),
[Map Loader guide](modules/maploader/README.md),
[Forge guide](modules/forge/README.md), and [PINE protocol](modules/forge/PINE.md).

## Build with Docker

From the repository directory in PowerShell:

```powershell
docker run -it --rm -v "${PWD}:/src" ps2dev/ps2dev:v1.2.0
```

Inside the container:

```sh
cd /src
sh ./docker-init.sh
make clean && make
```

Initialization installs build tools and downloads pinned librac3 (campaign) and
libuya (multiplayer) sources. Make compiles regional copies as needed. Outputs
remain on your computer in `build/` and `hostfs/uya/mods/`. Run initialization
again for each new container because `--rm` removes its installed dependencies.

### Individual modules

```sh
cd /src/modules/forge
make clean && make           # all supported regions
make REGION=pal             # PAL only
```

Modules build independently. Build from `/src` to produce the loader PNACH too.
Regions are `ntscu`, `pal`, and `ntscj`, where supported.

### Root commands

```sh
make                         # loader and all modules
make REGION=ntscu            # NTSC-U only
make module NAME=forge       # only Forge
make release                 # release ZIPs and checksums
```

Root `make clean` removes generated build files and regional hostfs module
binaries, preserving sources, configuration, and the optional exception handler.
Inside a module folder it cleans only that module.

The Dockerfile also provides a reusable initialized image for builds and CI.
For an existing toolchain, shared Make rules accept `LIBRAC3_DIR`, `LIBUYA_DIR`,
and `EE_PREFIX` overrides. No Python is required.

## Create a module

Run from the repository root inside the initialized container:

```sh
make new-module NAME=my-mod
make new-module NAME=campaign-mod LIBRARY=librac3
make new-module NAME=multiplayer-mod LIBRARY=libuya
make new-module NAME=shared-mod LIBRARY=both
```

This creates a folder under `modules/` with starter C code, a Makefile, and
`Makefile.defs`, then adds disabled entries to the module list. Enable them
before testing. Existing folders are not overwritten.

| Library | Output |
| --- | --- |
| `none` (default) | One image using the loader API in either mode |
| `librac3` | Campaign image |
| `libuya` | Multiplayer image, NTSC-U/PAL |
| `both` | Separate campaign and multiplayer images from one folder |

`both` creates `<name>-campaign.<region>.bin` and
`<name>-multiplayer.<region>.bin`; each uses its own library. Do not link both
libraries into one image.

Configure normal Make variables in `Makefile.defs`:

```make
LIBRARY = none
MODE = both
REGIONS = ntscu pal ntscj
EE_OBJS = main.o settings.o
```

Use `CAMPAIGN_OBJS` and `MULTIPLAYER_OBJS` for `LIBRARY=both`. Shared rules in
`Makefile.eeglobal` compile, link, and package relocation data. The loader picks
addresses at runtime; creators do not need fixed memory locations. Older
`module.json`/`module.conf` modules should migrate to `Makefile.defs` and rebuild.
Only the loader uses `template/` and `bin2code` to generate PNACH files.

## Module APIs

Mods need [module.h](module.h); Mod Menu is optional.

- `moduleInit(api)` runs once when the module first becomes eligible. A negative
  result disables it. BSS starts zeroed and persists across context changes.
- `moduleUpdate()` runs on matching sync callbacks, potentially more than once
  per video frame. Check game/player readiness before calling game functions.
- The API pointer remains valid and reflects the current mode/context. Use
  `api->print/open/read/close/flushCache` for resident SDK services.
- `setLifecycle(activate, deactivate)` handles re-entry and cleanup.
  `restrictContexts()` may narrow, but never expand, the configured contexts.
  Restore only owned hooks and release input/UI when deactivating. Memory stays
  reserved; stopping updates does not undo arbitrary patches.
- `publishService` / `findService` expose versioned APIs between active modules.
  Retry lookup if the provider is not ready. Loaded does not mean active.
- `loadConfig` / `saveConfig` store settings in
  `uya/config/<mod-name>.<region>.ini`, independently of Mod Menu.
  See [CONFIG.md](CONFIG.md) for examples and return codes.

### Module discovery and exports

The fixed loader header at `0xa0000` points to a linker-placed module table.
Use `UYA_LOADER_DISCOVERY` and `UYA_MODULE_TABLE` from `module.h`; validate their
magic, version, sizes, and count before following pointers. Entries describe
filenames, addresses, allocated sizes, and modes. Do not modify the table.

Every module begins with a 16-byte discovery header containing an optional
exports pointer. Define `moduleExports` with `UYA_MODULE_EXPORTS` to expose
module-owned data; omit it when unnecessary. Include a magic, version, and size
in your exports structure, and keep exported storage valid for the module's
lifetime. Use services for context-aware function calls.

External clients must validate pointer bounds and readiness, and rediscover
addresses after restart or savestate load. The [Forge PINE guide](modules/forge/PINE.md)
documents the complete loader → module table → Forge → mailbox lookup.

## Memory and compatibility

The loader does not resize the game's heap. Module allocations are 64-byte
aligned, follow `modules.txt` order, and last until restart.

| Range (end exclusive) | Use |
| --- | --- |
| `0x98000–0x9e000` | Optional exception handler and workspace |
| `0xa0000–0xa0030` | Fixed loader discovery header |
| `0xa0030–_loader_end` | Loader code, data, module table, and state |
| Aligned `_loader_end` to `0x100000` | Normal module pool |
| `0x02000000–0x08000000` | Expanded-RAM module pool |

When PCSX2 exposes 128 MiB and the BIOS provides `_InitTLB`, modules use expanded
RAM. The loader and exception handler stay fixed. Otherwise modules use normal
RAM. The eight-module limit is unchanged; module binaries work in either pool.

**An older BIOS can still crash during campaign/multiplayer ELF transitions
with expanded RAM enabled**, even when modules fall back to normal memory.
BIOS 1.20 failed this test; BIOS 2.30 passed the tested PAL scenarios. Disable
expanded RAM or use a compatible BIOS, then restart. The optional `bios-warning`
module requires Mod Menu and displays a warning; it cannot fix the BIOS.

### Optional exception handler

Place the supplied payload at `uya/mods/debug/exceptionhandler.bin` and restart.
Release ZIPs carry it in `uya/mods/utils/` for optional copying into `debug/`.
It is independent of `modules.txt` and consumes no module slot. Remove or rename
it to disable it; for source builds, also remove/rename the source copy to keep
it disabled after rebuilding. The loader checks the payload and reserved memory
before installing it.

### Regions and online support

Campaign builds support NTSC-U, PAL, and NTSC-J. Multiplayer modules support
NTSC-U/PAL; NTSC-J multiplayer callbacks remain disabled until addresses and a
regional library are available. Build success alone does not verify gameplay.
Live NTSC-U/PAL coverage and remaining limitations are recorded in
[ADDRESSES.md](ADDRESSES.md) and the module guides.

Default modules deactivate at the post-DNAS profile screen and reactivate on
return to Local Play. The loader uses a separate hook from Horizon. The user
confirmed online patch loading without a crash on the current NTSC-U setup.
Older Horizon builds that use `0xa0000` as their network callback slot conflict
with the loader; this was observed in an earlier PAL test. This result does not
qualify every online game mode or module combination. Avoid other cheats that
occupy the same memory or hook locations.

## Releases

`make release` builds all regions and creates region-specific and combined ZIPs
plus SHA-256 checksums under `release/zips/`. Packages contain `uya/`, matching
PNACH files, installation instructions, and Horizon's map downloader scripts.
Run those scripts from the host root alongside `uya/`; map packs are separate.

[release/modules.txt](release/modules.txt) selects production modules. Tests,
examples, local INIs, and downloaded maps are excluded. Supporting module files
belong in `uya/mods/utils/`. `make clean-release` removes generated release
outputs while preserving release policy and documentation.

The [GitHub workflow](.github/workflows/release.yml) builds and validates the
archives. Run it manually for artifacts, or push a `v*` tag to create a draft
release with ZIP attachments. Review and publish the draft when ready.
