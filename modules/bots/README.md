# Bots

Port of `uya-cheats/multiplayer/test/bots.c` and its training code.
NTSC-U and PAL multiplayer builds; campaign and NTSC-J are not supported.

```text
modmenu campaign,local,online
bots local,online
```

In **Mod Menu / Bots**, choose **0-3 bots** and **Idle**, **Passive**, or
**Aggressive** behavior. Defaults are 0 bots and Idle. Bot count applies to the
next game or reload; behavior changes apply immediately. Choices stay in module
memory between games, reloads, and online/local transitions, without an INI or
Save button. Restarting emulation restores defaults.

`bots.c` handles simulated players and controller pads. `training/` holds the
original training structures and movement/aiming routines. The experimental
five-bot timer and flyby patches are excluded. More than three bots is deferred.
Humans and bots together must fit the game's four local-hero slots.

## Online

The module runs online as host-side training AI and only uses vacant player
slots. It does not add network clients or synchronize new bot players to other
consoles. This preserves the original local training behavior.

Bots only activates online when its entire image is below Horizon's reserved
area at 0xc8000, or in expanded RAM. Otherwise it stays Local Play only.
PCSX2's 128 MiB option provides more room; use a compatible BIOS.
With Mod Menu enabled online, the separate MOD MENU pause row provides the
Bots tab. Selections also remain available when Mod Menu is disabled; Bots
does not depend on an active menu.

NTSC-U Local Play: the user confirmed three Idle bots, controls/HUD and the
Bots tab. Live reload checks retained count/behavior, and selecting zero bots
removed them on reload. NTSC-U Hoven also loaded online with Horizon; debugger
reads confirmed all three bots active and the online session flag restored.
The user also confirmed Marcadia loading online and the online Bots tab.
Other maps and PAL gameplay
still need live verification.
