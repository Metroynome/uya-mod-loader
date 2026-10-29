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
local-player count or receive their own HUD drawing pass. Simulated Triangle
weapon switches use the equipment API instead of opening a quickselect canvas.

`bots.c` handles shared player creation, simulation, controller pads and cleanup.
`online.c` owns synchronized network objects, ownership, roster publication and
network pad callbacks; `online.h` is its private interface. `main.c` owns the
module lifecycle and menu. `training/` holds the
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

## Build options

Synchronized online bots are included by this line in `Makefile.defs`:

```make
EE_DEFS += -DONLINE_BOTS
```

Comment it out to omit `online.c` and the Network Bot Prototype controls.
Local Play and the original host-only online training behavior remain available.
After changing the define, run `make clean && make` from `modules/bots/` inside
the build container.

## Online

### Network bot prototype

In an online game you host, enable **Network Bot Prototype** before
spawning any bots. Choose **Spawn Bot** to fill vacant player slots, up to
seven bots when only the human host is present.
The option is not saved; online games never spawn bots automatically.

NTSC-U has live host/PS2 coverage. PAL networking addresses are matched to the
regional memory dumps and the PAL build is enabled, but **PAL online is untested**.
Its menu help marks that status. No PAL online gameplay or kick/respawn was tested.

Bots use native player network objects and the shared roster. Horizon's
`feature/moosebot` receiver completes their creation during gameplay, using
remote actors without additional local controllers, viewports or HUDs.
Both systems must reconnect to download the updated local Horizon patch.

Extra guests reserve an unused network-object ID through the native session
allocator; the host's normal range and other clients' reservations stay intact.

Network bots reuse Horizon's existing player-index synchronization packets.
Only host-owned guest bots use this path when New Player Sync is off; humans
keep native synchronization. Bot native pad packets are suppressed to avoid
sharing the native protocol's single guest-input channel. No new server-plugin
message is required. NTSC-U Idle/movement was verified with six bots and two
human players on the host and PS2 client.

One Idle bot was verified on both the host and PS2 in Blackwater Docks, with
its own body and in-game roster entry. Its simulated pad feeds the registered
raw-input callback so native input starts neutral rather than empty.

Kick deletes only the selected bot's native object and republishes the roster.
The receiver retains native per-player cleanup while skipping shared-client
cleanup for tracked bots. NTSC-U removal of both the body and scoreboard entry was verified on the PS2;
respawning the removed slot also succeeded. Client join/leave feed messages
remain a follow-up.
Humans and bots share the eight-player limit; occupied human slots are never
replaced. Seven synchronized bots with only the host has not been tested.

### Host-side training bots

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

PAL address verification: the networking routines and class-name literal match
across all eleven PAL multiplayer dumps (lobby and ten retail maps). The native
creation call references class index `0x0024063c` and name `0x00241678`; its
world pointer/ID flags use `0x001d8788`/`0x001d87b0`, and player reporting uses
`0x00196468`. Every PAL leave entry matches the scoped cleanup-call layout.
