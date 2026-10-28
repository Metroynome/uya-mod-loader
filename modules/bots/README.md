# Bots

Port of `uya-cheats/multiplayer/test/bots.c` and its training code.
NTSC-U and PAL multiplayer builds; campaign and NTSC-J are not supported.

```text
modmenu campaign,local,online
bots local,online
```

In **Mod Menu / Bots**, choose **0-7 bots** and **Idle**, **Passive**, or
**Aggressive** behavior. Defaults are 0 bots and Idle. Bot count applies to the
next game or reload; behavior changes apply immediately. Choices stay in module
memory between games, reloads, and online/local transitions, without an INI or
Save button. Restarting emulation restores defaults.

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

NTSC-U Local Play: the user confirmed seven Idle bots in Hoven Deathmatch,
eight scoreboard entries, controls and HUD. Reload checks retained the seven
active records without changing the game mode or local-player count.
Changing the count to zero and reloading restored the native lookup hook;
selecting seven and reloading created all seven again.
NTSC-U Hoven also loaded online with Horizon with three bots; debugger
reads confirmed all three bots active and the online session flag restored.
The user also confirmed Marcadia loading online and the online Bots tab.
Other maps and PAL gameplay
still need live verification.
NTSC-U Bakisi Deathmatch also ran online with Horizon and all seven bot
records active. Debugger checks confirmed Deathmatch settings, restored
constructor instructions and the online session flag. The extra four actors
used native remote-player objects; their private camera/network-data buffers
total 5,056 bytes. The user confirmed all eight players were present and
spectating every bot worked after refreshing the extra actors' FPS-camera
positions with the native camera routine.
