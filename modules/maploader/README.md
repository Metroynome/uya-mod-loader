# Map loader

`maploader` is an independent module. It registers a **Custom Maps** tab through
the public `modmenu` API and adapts the WAD/world/sound/radar replacement flow
from `Horizon/horizon-uya-patch/patch/maploader.c`.

## Installation

Keep the Horizon map pack beside `module.txt`, separate from module binaries:

```text
host0:uya/
  module.txt
  maps.ntscu.idx
  maps.pal.idx
  aquadome.version
  aquadome.wad
  aquadome.world
  aquadome.sound
  aquadome.map
  aquadome.pal.wad
  aquadome.pal.world
  ...
  mods/
    maploader.ntscu.bin
    maploader.pal.bin
    modmenu.ntscu.bin
    modmenu.pal.bin
```

Index the **actual** host filesystem folder after adding, changing or removing
maps:

```powershell
python tools/index_maps.py "D:/PS2/ISOs/uya"
```

For maps kept in this repository's `hostfs/uya`, use `make maps`. A full build
also indexes and packages those local assets into `release/hostfs/uya`. Map
payloads and generated indexes are ignored by Git. External host folders are
not overwritten by a build.

The indexer reads Horizon's existing 48-byte `.version` header and regional
payload sizes. It leaves map files untouched and sorts the catalog by display
name. The runtime uses resident `host0:` services, so it does not download or
install Horizon's USB IRXs. Filenames may contain spaces. NTSC-U uses `.wad`,
`.world`, `.sound`, `.map`; PAL uses `.pal.wad`, etc., with the same `.version`.

Enable:

```text
modmenu 3
maploader 3
```

Build and deploy **all** binaries together; adding this module changes the
linked addresses of other modules. Cold-boot after deployment.

## Local Play

1. Enter Local Play staging.
2. Open the menu with L3 + R3 and select the Custom Maps tab with L1/R1.
3. Select a map with Cross. Its retail base map is set automatically.
4. Close the menu and start the match normally.

"None" clears the override and restores the previous base selection while
still in staging. Browse the entire catalog with Up/Down; X commits the
highlighted entry and closes the menu. The highlighted map stays centered in
the right-hand list; the left pane shows the author and description from the
map pack?s `.version` metadata when available. There are no pages, and up to 96 maps per region are supported.
Re-index and reboot after changing map files.

All retail bases 40–49 remain selectable in Local Play. The backend accepts
the base ID directly or its split-screen counterpart where the game uses one;
it does not force a nonexistent 56–59 overlay. A map's geometry/gameplay must
also be suitable for split screen; a valid base ID alone cannot guarantee that.

## Online and campaign status

Maploader disables itself by default when a multiplayer lobby-server connection
is active: its tab disappears, the selection clears, and it restores only hooks
that it still owns. `host0:uya/maploader-online.txt` defaults to `0`. Set its first
character to `1` and reboot to opt into online use on a future server without
Horizon. Missing or invalid configuration keeps online use disabled.

This opt-in does not add host/client map synchronization: all clients must select
the same map/version, with the host selecting its retail base. It also does not
make the full standalone loader compatible with Horizon's overlapping memory
range. Do not enable both systems together.

The module is available in campaign to explain its status, but **campaign
replacement is not implemented**. Campaign needs a separately verified loading
backend and campaign map assets. Multiplayer maps are never redirected into
campaign. The catalog rejects campaign base IDs for now. NTSC-J is not built
because its multiplayer resident layout is not verified by the parent loader.

## Supported payloads and failure handling

- WAD and/or world replacement, with TOC sector sizes updated before allocation.
- Optional sound bank, uploaded before the level WAD reuses its staging buffer.
- Optional radar map with separately checked online/local overlay hooks.
- Custom names in staging and on the loading screen; stock loading-screen art remains in use.
- Maps requiring a forced Horizon game mode or `.code` extension are excluded;
  this module does not supply Horizon's custom-code/game-mode ABI.

Selection checks file sizes again. TOC replacement checks the target base and
files before modifying allocations. Reads handle short transfers and verify
the expected file length. A failed read **after replacement starts stalls the
load** instead of allowing partially replaced map data to run. Fix the files,
re-index and restart emulation. Do not change payload files during a match/load.
Sound RPC waits are bounded; failure also stops the load.

The backend does not resize the heap. It reads into buffers allocated by the
game from the updated TOC. Catalog and loader state live in module BSS.

## Source and verification

- `main.c`: menu registration, scrolling list and selection.
- `files.c`: catalog validation and bounded hostfs reads.
- `multiplayer.c`: regional staging access and guarded loading hooks.
- `tools/index_maps.py`: map-pack catalog generation.
- `tools/verify_maploader.py`: checks loading/seek anchors and radar signatures
  against the supplied retail extraction.

```powershell
python tools/verify_maploader.py "D:/PS2/ISOs/RAC Levels/rac3"
```

NTSC-U/PAL builds and native tests pass. Both regional loading-hook sets,
resident seek anchors, ten online radar hooks and six local radar hooks were
checked against the extracted binaries. NTSC-U Local Play staging was inspected
live: menu identity, settings pointer and base-map field were verified.
In an NTSC-U Local Play test, Aquatos Aquadome progressed through the replacement
TOC and reached loaded gameplay (base 43, scene-ready flag set), with the custom
sound upload completed, no loader failure flag, and the radar hook installed.
The user confirmed that Aquatos Aquadome loaded. PAL and multi-controller
split-screen loads have not been tested live.

The standalone loader also occupies Horizon's `CUSTOM_GAME_MODE` range,
`0x000f0000`–`0x00100000`. Full Horizon-patch coexistence needs an explicit
memory/hook integration; simply enabling both is not supported.

## Execution contexts

The default `module.txt` now uses `maploader local`. Loader API 3 disables it at
the post-DNAS profile screen and invokes its cleanup callback to remove the tab
and owned map hooks. On returning to Local Play it registers its tab and hooks
again. The optional `maploader-online.txt` flag does not override `module.txt`:
future online use requires both an online-capable context entry and that flag.
This is not support for coexistence with the full Horizon patch's overlapping
memory layout.
