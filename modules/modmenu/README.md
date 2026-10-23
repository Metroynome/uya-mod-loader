# Mod menu

`modmenu` provides a shared menu for other modules in campaign and multiplayer.
It does not require a server connection or Horizon patch. Enable both the menu
and your consumer in `hostfs/uya/config/modules.txt`, then build and deploy:

```text
modmenu       campaign,local
menu-example  campaign,local
```

In front-end menus, press **L3 + R3** to open or close. During gameplay this
shortcut is disabled; use the multiplayer pause-menu **MOD MENU** entry. Use **L1 / R1** to change tabs, **Up / Down**
to select a control, **Left / Right** to change a value, **Cross** to activate,
and **Triangle** to close. The menu closes on level/mode changes and while its
renderer is unavailable. It suppresses processed controller input and lobby
menu events while preserving raw button samples for stable press/release edges.
It does not pause the game or stop online simulation.

NTSC-U/PAL use campaign and multiplayer backends. NTSC-J currently supports
campaign only, matching the loader's existing multiplayer limitation.

## Add controls from another module

Include `include/modmenu.h` from your module. No link-time dependency on the
menu binary or fixed export address is needed. Keep the loader API pointer from
`moduleInit`, then retry `modmenuFind` from `moduleUpdate` until it succeeds.
This supports either manifest order and lets your mod run when the menu is
disabled. See [menu-example/main.c](../menu-example/main.c) for a complete
consumer with a toggle, integer range, and action.

```c
#include "include/modmenu.h"
static const ModuleApi *loader;
static int registered, enabled;

int moduleInit(const ModuleApi *api)
{
    loader = api;
    return 0;
}

void moduleUpdate(void)
{
    const ModMenuApi *menu;
    int tab;
    ModMenuItem item = {
        "Enable feature", MODMENU_TOGGLE, &enabled, 0, 0, 0, 0, 0
    };
    if (registered) return;
    menu = modmenuFind(loader);
    if (!menu) return;
    tab = menu->addTab("my-mod", "My Mod", UYA_MODE_BOTH);
    if (tab < 0) return;
    if (menu->addItem(tab, &item) < 0) {
        menu->removeTab(tab);
        return;
    }
    registered = 1;
}
```

Choose the tab's mode mask to match the code it calls: `UYA_MODE_CAMPAIGN`,
`UYA_MODE_MULTIPLAYER`, or `UYA_MODE_BOTH`. A campaign library callback must
not be exposed in multiplayer. For modules with separate campaign/multiplayer
images, use distinct tab keys and the corresponding mode for each image.
Check player/level readiness inside gameplay callbacks, including at the main
menu. The menu cannot determine whether an arbitrary game function is safe.

## API contract

The versioned table is declared in [include/modmenu.h](../../include/modmenu.h).
Registration returns a positive opaque handle, or a negative value on failure.
Mutation functions return zero on success, negative on failure.

| Function | Behavior |
|---|---|
| `addTab(key, label, modes)` | Registers a unique key and a mode-filtered tab. |
| `addItem(tab, definition)` | Copies a control into an existing tab. |
| `removeTab(tab)` | Removes the tab and its controls; old handles become invalid. |
| `setItemEnabled(item, enabled)` | Enables/disables selection and activation. |
| `isOpen()` | Returns whether the menu is open. |
| `close()` | Closes the menu. |
| `getActiveTab()` | Returns the selected tab handle, or zero. |
| `focusTab(tab)` | Focuses a visible tab and resets its control selection. |

Supported controls:

- `MODMENU_ACTION`: renders a Horizon-style bordered button with centered text
  and invokes the required callback on Cross or Right. Disabled buttons are dimmed.
- `MODMENU_TOGGLE`: toggles the pointed-to integer between zero and one.
- `MODMENU_RANGE`: changes the pointed-to integer by a positive step, clamped
  to inclusive minimum/maximum bounds.
- `MODMENU_LABEL`: displays nonselectable text.

Toggle/range callbacks are optional and run after the value is changed.
Callbacks receive the supplied context and run synchronously on the game
thread. They must not block; they may remove their own tab. Store values and
contexts in persistent module storage, not local stack variables. Item
definitions and labels are copied, but value/context pointers are retained.
Use callbacks to update mod settings, and apply continuous behavior from your
own `moduleUpdate`.

There are eight tabs (one reserved by the menu's home tab) and forty controls
total. Tab keys/labels accept up to 31 bytes, item labels up to 47 bytes.
Overlong strings, duplicate keys, invalid modes/ranges and exhausted capacity
are rejected. Removed slots can be reused, but old handles are never reused.
Modules remain resident until emulation restarts; this is not a hot-unload API.
Registration persists across mode changes. Recheck service availability before
using a cached API after a mode transition.

## Backend and verification

`main.c` owns registration, input, selection and drawing, including the bordered
panel, tab strip and selection highlight. `backend.c` selects the appropriate
controller buffer, font functions and box-rendering context for each overlay.
The verified address table is checked in as C in `renderers.h`. Ordinary builds
need no extraction or generator.
Function and rendering-context fingerprints are checked at runtime before calling the renderer.
Unrecognized overlays leave the menu closed. Signature matches do not replace
live testing of every level and region.

Native tests cover registration, capacity, selection, mode filtering, input
edges, range bounds and removal. All regional binaries build. An NTSC-U boot
menu debugger trace confirmed service registration, game font calls returning,
successive updates and navigation to the example tab. Visual layout and
campaign/multiplayer gameplay transitions still require in-game verification.
The controller regression also runs the real backend across 100 captures of a
held L3 + R3 sample, verifies that the raw buttons remain intact, and checks
restoration/ownership of the lobby input hook.

`setList(tab, count, label, select, context, initial)` registers a virtual scrolling
list without consuming item slots. The label callback returns text for an index;
the select callback receives the highlighted index only when X is pressed.
Callbacks and context must remain valid until the tab is removed. Up/Down moves
the cursor and wraps; L1/R1 switches tabs. Lists use their tab's content area.

In NTSC-U/PAL multiplayer matches, the pause menu's Continue entry becomes
**MOD MENU**, using Horizon's return-hook approach. The retail callback closes
the pause screen before opening the mod menu. This is verified against all ten
base overlays and six local split-screen overlays. Campaign adds a separate **MOD MENU** row to the main pause menu, preserving
all retail entries. Campaign tables are verified for NTSC-U, PAL, and NTSC-J.
The shortcut remains available at boot. Verified addresses are stored in the C tables.

Virtual lists mirror Horizon's two-pane layout: nine rows wrap around a fixed
center highlight on the right, with right-aligned names. Providers can call
`setListInfo(tab, callback)` to supply text for the highlighted entry on the left.
Text wraps to the pane width. Tab widths use the game's actual font measurement;
the tab strip scrolls whole tabs to keep the active tab visible, with overflow
arrows. Closing through the API consumes the held selection button until release.

The Mod Menu home tab shows the menu release version, a short description, live
mode/region information, and controls. Set the release version in `version.h`;
it is separate from the public service ABI version.

## Tab visibility

Legacy `addTab(..., UYA_MODE_...)` behavior remains supported. For finer control,
call `setTabVisibility` after creating the tab:

```c
int tab = menu->addTab("my-mod", "My Mod", UYA_MODE_BOTH);
menu->setTabVisibility(tab, MODMENU_SHOW_MAIN_MENU | MODMENU_SHOW_MULTIPLAYER);
```

- `MODMENU_SHOW_MAIN_MENU`: campaign boot menu and multiplayer lobby/staging.
- `MODMENU_SHOW_CAMPAIGN`: campaign levels, including level 0 (Veldin).
- `MODMENU_SHOW_MULTIPLAYER`: multiplayer matches.
- `MODMENU_SHOW_EVERYWHERE`: all three contexts; `0` hides the tab.

Combine flags with `|`. The setter replaces the tab's legacy mode filter.
This controls UI visibility, not whether the module executes: keep the module's
manifest mode and library calls appropriate for the contexts it supports.
Campaign pause anchors are checked in alongside the renderer tables.

## Input capture

Campaign captures fresh controller polls, retaining the physical sample for menu
navigation while passing neutral buttons, sticks, and pressure values to the
game. The previous raw callback is restored after closing buttons are released.

Multiplayer front-end menus use the live raw sample and Horizon's lobby input
dispatcher patch. During matches, the menu also captures the local player's PAD,
verified through `Player.pPad`. It neutralizes input before invoking the original
callback so camera, zoom, and the recorded player stream cannot receive menu
inputs. Multiplayer's raw/history address is PAD + 0x80; never treat that raw
address as the start of the controller structure.

The `Player.timers.noInput` lock is also refreshed for every local player,
including split-screen players. NTSC-U/PAL online levels 40-49 and Local Play
levels 50-55 have separately verified overlay references. Remote players are
untouched and longer retail input locks are preserved. The timer expires
naturally after capture ends.

## Local Play and online tab filtering

`setTabContexts(tab, mask)` uses the same `UYA_CONTEXT_*` flags as the loader.
It is independent of the existing screen filter `setTabVisibility`:

```c
menu->setTabContexts(tab, UYA_CONTEXT_CAMPAIGN | UYA_CONTEXT_LOCAL);
menu->setTabVisibility(tab, MODMENU_SHOW_MAIN_MENU);
```

This example shows the tab in campaign boot and Local Play menus, but never in
online menus or gameplay. Omit the screen restriction to show it in gameplay
as well. Existing tabs default to all contexts. Hiding a tab does not disable
the mod: use configuration or `restrictContexts` to restrict execution, and
remove registered tabs in your mod's deactivation callback (see menu-example).

## Saving and loading mod settings

Persistence belongs to the mod loader's `ModuleApi`, so mods can use it without
Mod Menu. See the [configuration API](../../CONFIG.md). The original menu
`loadConfig` and `saveConfig` functions remain as forwarding wrappers for
compatibility, with the same results and the new `host0:uya/config/` location.

The Example Mod initializes its settings through the loader before attempting
to register its tab. Its Save settings / Load settings actions also call the
loader directly.

### Progress popup

The optional `setProgress(label, current, total)` API displays a modal progress bar and captures game input. Pass a nonpositive total while discovering work, then a positive total for measured progress. Pass `NULL` as the label to dismiss it, including when your mod deactivates. Check the API structure size before using this appended field.

While Mod Menu or a progress popup captures input on the campaign boot menu,
the preview-movie idle countdown is reset. Closing the menu lets the normal
countdown begin again; campaign levels and multiplayer are unaffected.
