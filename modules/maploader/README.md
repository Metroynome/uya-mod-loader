# Map Loader

`maploader` loads Horizon-format hostfs map packs in NTSC-U/PAL Local Play.
Mod Menu provides the Custom Maps tab; Forge adds in-game hopping and refresh
commands. Campaign replacement and NTSC-J multiplayer are not implemented.

## Setup

Enable these entries in `uya/config/modules.txt`:

```text
modmenu campaign,local
maploader local
# Optional: build/copy Forge, then enable it for refresh and in-game hopping.
forge local
```

Keep map assets in `uya/`, separate from module binaries:

```text
uya/
  config/modules.txt
  aquadome.version
  aquadome.wad
  aquadome.world
  aquadome.sound
  aquadome.map
  aquadome.pal.wad
  aquadome.pal.world
  ...
  mods/
    modmenu.<region>.bin
    maploader.<region>.bin
    forge.<region>.bin
    utils/mapscan.irx
```

NTSC-U uses `.wad`, `.world`, `.sound`, and `.map`; PAL uses `.pal.wad`, etc.
Both share `.version` metadata. The helper enumerates files offline without
Horizon, USB drivers, or Python. See the [root README](../../README.md#build-with-docker)
for builds. Modules are placed dynamically; adding one does not require
rebuilding unrelated modules. Install compatible binaries and cold-boot.

## Select maps

1. Enter Local Play staging and wait for the first scan to finish.
2. Open Mod Menu with L3 + R3 and switch to Custom Maps with L1/R1.
3. Browse with Up/Down and select with Cross. The menu closes automatically.
4. Start the game normally.

`None` appears first, followed by retail bases 40–49 and up to 96 custom maps
in alphabetical order. The highlighted entry stays centered on the right, with
its author/description on the left. There are no pages.

Selecting a custom map sets its retail base automatically. Selecting a retail
map clears the custom override and establishes the baseline. In staging,
`None` restores that baseline after a custom selection. The preview uses the
retail base's image; custom artwork is not loaded. Staging/loading names use the
selected map's name. Retail names are English.

With Forge active, the list is also available through the in-game pause menu's
MOD MENU entry. Selecting a named retail/custom map hops to it; `None` remains
staging-only. See the [Forge guide](../forge/README.md).

## Scanning and refresh

The first Local Play staging visit after activation scans `host0:uya/`. Entering
multiplayer menus or online staging does not scan. The progress popup captures
input while discovery and metadata processing run across updates.
Each update batches up to 32 scan steps within a 2 ms budget, reusing the same
buffers. Individual synchronous file reads can exceed that budget.

A successful catalog, including an empty one, stays in one heap allocation and
is reused after leaving staging or quitting a game. **No `.idx` files are used**;
old index files can be removed. Filesystem changes are not detected automatically.

Use **Forge → Refresh Custom Maps** or a [PINE refresh command](../forge/PINE.md)
after adding, removing, or updating maps. You can also press Square while the
Custom Maps tab is focused in Local Play staging, without Forge. An empty catalog
shows a **Refresh Custom Maps** button and **Browse Retail Maps**. Square also
works in-game when Forge is active. Refresh is blocked during scans or loading.
Refresh reuses the allocation and retains selection by filename when possible.
There is no backup catalog: a failed scan clears selection and makes the list
unavailable. Retry by refreshing or re-entering staging, then select again.

Deactivation releases the catalog. On a campaign ELF change, stale multiplayer
pointers are discarded instead of being passed to the campaign allocator.
After leaving Horizon online play, Map Loader reclaims its loading hooks only
in Local Play staging and only when all Horizon callback signatures match the
verified regional builds. Unknown hooks stay untouched; the tab reports that
loading hooks are unavailable rather than presenting a completed scan as a
missing map list.

The IOP helper remains available until an IOP reset. Do not alter map files
while scanning or loading a game.

## Supported assets and failures

- WAD/world replacement, with TOC sizes updated before game allocations.
- Optional sound bank and radar map.
- Custom staging and loading-screen names, including Forge hops.
- Retail loading-screen art and staging previews.

Maps requiring Horizon custom game modes or `.code` extensions are excluded;
this module does not implement that ABI. Campaign base IDs are rejected.
Split-screen base IDs are handled where the game supports them, but this does
not guarantee that a particular map works with multiple players.

Selection and loading validate asset sizes; reads handle short transfers and
check the expected length. Failure **after replacement begins stops the load**
to avoid running partial data. Fix the assets and restart the game. Sound RPC
failures also stop loading; a stalled transition is not guaranteed recoverable
through the menu.

The backend does not resize the heap. Map data uses game-allocated buffers;
the catalog is heap-resident and its bookkeeping lives in module storage.

## Contexts and online compatibility

`maploader local` deactivates at post-DNAS profile selection, removing its tab
and restoring owned hooks. It reactivates on return to Local Play.

Future online use requires both an online-enabled `modules.txt` entry and
`uya/maploader-online.txt` starting with `1` (default `0`). This does not add
host/client synchronization or qualify Map Loader for online use. The user
confirmed Horizon patch loading without a crash on the current NTSC-U setup,
with offline modules disabled. An older PAL patch conflicted with the loader's
`0xa0000` discovery header. See [ADDRESSES.md](../../docs/ADDRESSES.md).

## Verification and source

NTSC-U/PAL builds and native checks pass. Live tests covered custom-map starts,
reloads, retail/custom hops, HUD restoration, catalog retention, and retail
previews. PAL Aquatos Aquadome passed start/reload and hops to/from Bakisi Isles;
the user also confirmed its corrected loading-screen name. Temple of Shaar
still crashes on PAL during normal starts and hops. Multi-controller
split-screen loading has not been live-qualified.

| File | Responsibility |
| --- | --- |
| `main.c` | Menu, selection, and public service |
| `files.c` | Catalog validation and hostfs reads |
| `multiplayer.c` | Staging, names, and guarded loading hooks |
| `hop.c`, `hop_addresses.h` | Regional transitions and HUD setup |
| `directory.c`, `iop/mapscan/` | Directory scanning RPC |

## Filename/protocol update

Map filename stems now accept up to 128 ASCII characters (plus the NUL terminator).
Update Forge, Map Loader, and `mods/utils/mapscan.irx` together, then cold-boot.
External clients must use [PINE protocol v2](../forge/PINE.md); the v1 field offsets are incompatible.
