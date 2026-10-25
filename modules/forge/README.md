# Forge

Forge provides Local Play map-testing controls for NTSC-U and PAL. It uses Map
Loader to reload or change maps without leaving the current game. No changes
to Horizon Forge v2 are included; its developer can integrate the documented
[PINE command interface](PINE.md).

## Setup and use

Forge and its dependencies are enabled in NTSC-U/PAL releases. For a custom
module list, use these entries in `uya/config/modules.txt`:

```text
modmenu local
maploader local
forge campaign,local
```

Restart emulation after installing binaries or changing `modules.txt`. See the
[root build instructions](../../README.md#build-with-docker).

- **Reload Current Map:** open MOD MENU from the in-game pause menu, then choose
  Forge → Reload Current Map. Works with retail and custom maps.
- **Change maps:** choose a named map in the in-game Custom Maps list and press
  Cross. The menu closes and a normal loading transition begins.
- **Game Mode:** scroll left/right through Siege, CTF, and Deathmatch. In staging,
  the settings and mode label update immediately. During gameplay, the choice
  stays pending until Reload Current Map or a map hop. Changing modes uses the
  native objective defaults and team rules; unchanged modes retain their rules.
  Save/Load Settings includes the selected mode. Each new staging/game context
  starts with its actual mode, as in DL Forge.
- **Quick Load Maps:** bypasses loading-loop VSync waits for custom maps when
  starting, hopping, or reloading. Off by default; Save/Load Settings retains
  the toggle in `uya/config/forge.<region>.ini`. Retail maps keep normal VSync.
  No loading popup is added, and normal frame timing returns after loading.
- **Chargeboots / Infinite Ammo:** both default to ON, matching DL Forge.
  Forge applies these game rules in Local Play staging and gameplay. Turn either
  off in the Forge tab; Save/Load Settings retains both choices per region.
- **Refresh Custom Maps:** use the Forge tab in staging or supported gameplay
  after changing map files. Refresh updates the catalog; it does not reload the
  current level. Without Forge, Square refreshes the focused Custom Maps tab
  in Local Play staging.
- **Boot to Local Play:** off by default, preserving normal startup. When on,
  cold startup follows the native Local Play/Create Game flow to staging without starting
  a game. Save Settings retains the toggle; Load Settings restores it from
  `uya/config/forge.<region>.ini`. Turning it off keeps normal startup.
  Enable Forge for `campaign,local` so it can run before the multiplayer ELF.
  PINE command 1 can request staging from the boot menu while the setting is off.

Retail and custom maps share an alphabetical list. `None` is staging-only;
choose a named retail entry to leave a custom map during gameplay. The in-game
list is available only while Forge is active. L3 + R3 remains disabled during
gameplay; no reload button combo is implemented.

Hops use the existing game session and settings, with player recreation and HUD
setup. They are full loading transitions, not seamless geometry updates or a
promise to preserve transient player state, scores, or timers.

## External commands

The module-owned mailbox accepts:

| Command | Purpose |
| --- | --- |
| Enter Local Play | Boot menu to staging; preserves the saved boot option |
| Refresh | Rescan available map packs |
| Select/load filename | Select in staging; hop during gameplay |
| Refresh and select/load | Rescan before selection/hopping |
| Select/load retail | Select or hop to retail base 40–49 |
| Start Game | Start from staging with current map/settings |
| Reload current | Reload the active retail/custom map |

To start a newly selected map, wait for selection to complete, then send Start
Game with a new request ID. Accepted work is not necessarily finished: always
check completion and error fields. Busy or unsupported requests are rejected.

PINE works without Mod Menu. Discover the mailbox through the loader/module
headers; never hard-code its address. Selection, Start Game, and reload commands
accept an optional mode byte: 0 Siege (default), 1 CTF, or 2 Deathmatch.
[PINE.md](PINE.md) defines the exact layout, commands, ordering, readiness checks,
and error codes. Direct PCSX2 PINE tests passed; integration into the Forge v2
editor remains separate.

## Support and limitations

- Local Play staging and gameplay base overlays **40–49**, NTSC-U/PAL.
- No campaign, online, NTSC-J, or split-screen hopping.
- No automatic file watching, build-triggered reload, or position restoration.
- Map assets must pass Map Loader validation. Do not update files during a scan
  or load. A failure after teardown may require restarting the game.
- Mod Menu is optional for commands; Map Loader is required for map commands,
  but not Enter Local Play. Unavailable
  dependencies disable actions rather than enabling unsupported transitions.

Live NTSC-U tests covered Temple of Shaar reloads, hops to Bakisi Mini, repeated
reloads, retail Korgon Outpost, and return hops with working gameplay/HUD.
PAL tests covered Bakisi Isles and Aquatos Aquadome starts/reloads and hops in
both directions, including the corrected custom loading-screen name.
PAL also passed PINE CTF selection/Start Game, a reload defaulting to Siege,
and invalid-mode rejection. Aquadome quick reloads measured about 4.8 seconds
versus 11 seconds with Quick Load off on the test emulator; retail timing is unchanged.
Temple of Shaar's PAL normal-start and hop crashes remain unresolved. Other map
bases/player counts are not all live-qualified. See [ADDRESSES.md](../../docs/ADDRESSES.md).

## Ownership

Map Loader owns catalog scanning, validation, asset loading, and transitions.
Forge exposes its active capability, menu actions, and command mailbox through
the existing loader service API. Mod Menu supplies UI and input capture.
Deactivation removes Forge's tab and makes the mailbox unavailable. PINE does
not override the loader's offline context restrictions.

## Future suggestions

These are ideas, not implemented features:

- **Reload after a successful build:** an opt-in editor notification triggers a
  reload only after all assets are complete. Queue/coalesce notifications while busy.
- **Remember test position:** restore position and camera after same-map reloads,
  with normal-spawn fallback when geometry changes.
- **Richer status/errors:** show build IDs, missing assets, wrong regions, incomplete
  builds, and load duration so testers can identify what actually loaded.
- **Automatic update detection:** prefer explicit completed-build notifications
  over periodic scans. A revision marker or optional slow scan could support
  older exporters. Changed size/version alone cannot detect every content edit.
- **Optional reload shortcut:** consume its gameplay inputs and trigger only once
  per press. Horizon Deadlocked uses L1 + Up + Circle; no UYA shortcut is assigned.

For editor export, agree on a complete-build handoff with region, base map,
stable filename, build ID, and asset sizes/hashes. Publish readiness only after
all writes finish; avoid reading partially replaced files. Compute hashes on
the PC. Keep manual refresh/reload available without an editor connection.
Supporting files may use `uya/mods/utils/forge/`; map packs currently remain in
`uya/`. The implemented mailbox protocol is the starting point for integration,
not a proposed replacement protocol.

## Filename/protocol update

Map filename stems now accept up to 128 ASCII characters (plus the NUL terminator).
Update Forge, Map Loader, and `mods/utils/mapscan.irx` together, then cold-boot.
External clients must use [PINE protocol v2](PINE.md); the v1 field offsets are incompatible.
