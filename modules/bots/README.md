# Bots

Port of `uya-cheats/multiplayer/test/bots.c` and its training code.
NTSC-U and PAL multiplayer builds; campaign and NTSC-J are not supported.

```text
modmenu campaign,local,online
bots local,online
```

In **Mod Menu / Bots**, **Default Bots Spawned** chooses 0-7 automatic bots for
the next Local Play game or reload. It defaults to 0. **Save Settings** and
**Load Settings** use `uya/config/bots.<region>.ini` with the integer key
`defaultBotsSpawned`. Saved defaults load automatically on entering Local Play.
The default and save/load controls are hidden online, where every game starts
without automatic bots regardless of the saved value.

Choose **Idle**, **Passive**, or **Aggressive** behavior; Idle is the default.
Behavior changes apply immediately and stay in module memory between games
and online/local transitions. Behavior is not written to the configuration.

During a game, **Spawn Bot** adds a bot to a vacant player slot. **Bot List**
shows normal rows with the bot name on the left and a **Kick** button on the
right. Highlight Kick and press X to make that bot leave. Spawning displays
"<bot name> joined the game." in the native notification feed.
These actions do not change the Local Play default. Spawn Bot is disabled
when all eight player slots are occupied, counting humans and bots; reserved
online human slots are also protected. Online controls are host-only.
Removal uses the native per-player leave callback to clean up the hero,
equipment and player slot. Its client-wide ownership cleanup is skipped so
the shared human host and its objects remain intact.

Recreating a bot reuses its native weapon, timer and state-table entries.
This prevents kick/spawn cycles from exhausting those tables and corrupting
the human player's quickselect or weapon HUD. Bots do not increase the native
local-player count or receive their own HUD drawing pass.

`bots.c` handles simulated players and controller pads. `training/` holds the
original training structures and movement/aiming routines. The experimental
five-bot timer and flyby patches are excluded. Local Play supports up to seven
bots, with humans and bots sharing the eight roster slots. Occupied human
slots are never replaced.

The first three bots use the original local heroes. Additional bots use vacant
objects in the game's eight-player remote pool, with separate cameras and
network data. Controller/profile accesses stay within the four local slots;
the native local-player count and session type are unchanged. Constructor
patches are scoped to each creation, and the player-lookup hook is restored
when the game ends or reloads.

## Online

The module runs online as host-side training AI and only uses vacant player
slots. It does not add network clients or synchronize new bot players to other
consoles. This preserves the original local training behavior.
Online permits up to seven bots when the host has enough vacant roster slots.
Bots whose roster IDs exceed three reuse the game's remote-player objects;
their local AI simulation suppresses native pad broadcasts. The session flag
and client roster are restored after construction/simulation. Small private
camera and network-data buffers remain necessary for locally controlled AI;
there are no additional full player-object allocations.

Bots only activates online when its entire image is below Horizon's reserved
area at 0xc8000, or in expanded RAM. Otherwise it stays Local Play only.
PCSX2's 128 MiB option provides more room; use a compatible BIOS.
With Mod Menu enabled online, the separate MOD MENU pause row provides the
Bots tab. Selections also remain available when Mod Menu is disabled; Bots
does not depend on an active menu.

## Verification

NTSC-U Local Play: saved/default loading was verified with three automatic
bots. Filling the roster to eight players disabled Spawn Bot; removing a bot
re-enabled it without changing the saved default. Earlier seven-bot tests
confirmed scoreboard entries, HUD/controls, reloads and slot reuse.
The allocation-reuse fix also passed normal-speed kick/spawn checks in both
standard and extra slots with seven bots active: all four table counts stayed
unchanged and the host's weapon HUD continued updating. Online visual
verification of this fix remains pending.

NTSC-U online with Horizon: a saved Local Play default of three produced zero
automatic bots. The default and Save/Load controls were absent. Manual spawning,
kicking, named join/leave notifications and the eight-player limit passed
debugger checks while preserving the host and online session. Seven-bot Bakisi
spectating, Hoven/Marcadia loading and the online menu were also user-verified.
The extra four actors reuse native remote-player objects; private camera and
network-data buffers total 5,056 bytes.

Both NTSC-U and PAL build and relocation checks pass. PAL gameplay and other
maps still need live verification.
