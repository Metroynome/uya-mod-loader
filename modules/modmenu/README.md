# Mod Menu API

`modmenu` provides shared tabs and controls for campaign and NTSC-U/PAL
multiplayer. NTSC-J supports campaign only. No Horizon connection is required;
the default configuration keeps the menu offline.

```text
modmenu       campaign,local
menu-example  campaign,local
```

Use **L3 + R3** in front-end menus. During campaign or multiplayer gameplay,
choose **MOD MENU** from the pause menu instead. L1/R1 switch tabs, Up/Down
navigate, Left/Right adjust values, Cross selects, and Triangle closes.
The menu captures game input but does not pause simulation. It closes when the
level/mode changes or its renderer becomes unavailable. At the campaign boot
menu, input capture also resets the preview-movie timer.

## Register a tab

Include [modules/modmenu/include/modmenu.h](include/modmenu.h). Find the service from your
update callback so registration works regardless of module order. Your mod can
continue without Mod Menu when the service is unavailable.

```c
#include "modules/modmenu/include/modmenu.h"

static const moduleApi_t* loader;
static const modMenuApi_t* registeredMenu;
static int tab, enabled;

static void deactivate(void)
{
	if (registeredMenu && tab > 0)
		registeredMenu->removeTab(tab);
	registeredMenu = 0;
	tab = 0;
}

int moduleInit(const moduleApi_t* api)
{
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	loader = api;
	return api->setLifecycle(0, deactivate);
}

void moduleUpdate(void)
{
	const modMenuApi_t* menu;
	int handle;
	modMenuItem_t toggle = {"Enable feature", MODMENU_TOGGLE, &enabled, 0, 0, 0, 0, 0};

	if (tab > 0)
		return;
	menu = modmenuFind(loader);
	if (!menu)
		return;
	handle = menu->addTab("my-mod", "My Mod", UYA_MODE_BOTH);
	if (handle < 0)
		return;
	if (menu->addItem(handle, &toggle) < 0) {
		menu->removeTab(handle);
		return;
	}
	menu->setTabContexts(handle, UYA_CONTEXT_CAMPAIGN | UYA_CONTEXT_LOCAL);
	registeredMenu = menu;
	tab = handle;
}
```

Deactivation removes the tab and resets registration so it can return when the
module becomes active again. See [menu-example](../menu-example/main.c) for
settings, actions, and persistence. Register lifecycle callbacks from your own
init/update; combine tab cleanup with any other cleanup your mod needs.

## Controls and limits

| Control | Behavior |
| --- | --- |
| `MODMENU_ACTION` | Horizon-style button; invokes its required callback on Cross or Right |
| `MODMENU_TOGGLE` | Toggles the pointed-to integer between 0 and 1 |
| `MODMENU_RANGE` | Adjusts an integer by a positive step within inclusive bounds |
| `MODMENU_LABEL` | Nonselectable text |

Toggle/range callbacks are optional and run after the value changes. Callbacks
receive their context on the game thread; they must not block. Definitions and
labels are copied, but value/context pointers must remain valid until removal.
Use persistent module storage, not stack variables, for those pointers.

There are **8 tabs** (including the home tab) and **40 controls**. Tab keys and
labels allow 31 bytes; item labels allow 47. Invalid definitions, duplicate keys,
overlong strings, and exhausted capacity are rejected. Handles are positive and
opaque; removed handles become invalid. Registration failures return negative
values. Removed slots can be reused.

## API reference

The service ABI remains version 1, with extensions tracked by the table's
`size`. The current `modmenuFind` helper checks the full current structure size.
Code doing its own service lookup must check version and size before accessing
fields. Recheck availability across context changes.

| Function | Purpose |
| --- | --- |
| `addTab(key, label, modes)` | Register a tab filtered by `UYA_MODE_*` |
| `addItem(tab, definition)` | Copy a control into the tab; return its handle |
| `removeTab(tab)` | Remove a tab and its controls |
| `setItemEnabled(item, enabled)` | Enable or disable a control |
| `isOpen()` / `close()` | Query or close the menu |
| `getActiveTab()` / `focusTab(tab)` | Read or change the selected visible tab |
| `setTabVisibility(tab, mask)` | Set the screen filter described below |
| `setTabContexts(tab, mask)` | Set an independent loader-context filter |
| `setList(tab, count, label, select, context, initial)` | Register a virtual scrolling list |
| `setListInfo(tab, callback)` | Supply information for the highlighted list row |
| `setProgress(label, current, total)` | Show or dismiss a progress overlay |
| `setModal(draw)` | Claim or clear a module-owned popup |
| `drawBox(...)` / `drawText(...)` | Draw popup content using the current backend |
| `loadConfig(...)` / `saveConfig(...)` | Compatibility wrappers for loader persistence |

Ordinary mutation functions return zero on success and negative on failure.
`close`, drawing, and progress functions return no value. `setModal` returns
zero when another popup owns the slot; see the header for exact signatures.

### Visibility

Choose `UYA_MODE_CAMPAIGN`, `UYA_MODE_MULTIPLAYER`, or `UYA_MODE_BOTH` when adding
a tab. For more control, `setTabVisibility` replaces that initial screen filter:

- `MODMENU_SHOW_MAIN_MENU`: campaign boot menu and multiplayer lobby/staging.
- `MODMENU_SHOW_CAMPAIGN`: campaign levels, including Veldin.
- `MODMENU_SHOW_MULTIPLAYER`: multiplayer gameplay.
- `MODMENU_SHOW_EVERYWHERE`: all screens; `0` hides the tab.

Combine flags with `|`. `setTabContexts` independently filters by
`UYA_CONTEXT_CAMPAIGN`, `UYA_CONTEXT_LOCAL`, and `UYA_CONTEXT_ONLINE`:

```c
menu->setTabContexts(tab, UYA_CONTEXT_CAMPAIGN | UYA_CONTEXT_LOCAL);
menu->setTabVisibility(tab, MODMENU_SHOW_MAIN_MENU);
```

This shows a tab only in campaign boot and Local Play menus. Visibility does not
control module execution or make game APIs safe. Use `modules.txt` and loader
`restrictContexts` for execution restrictions, and check readiness in callbacks.
Separate campaign/multiplayer images should use distinct tab keys.

### Lists

Virtual lists consume no item slots. Up/Down wraps around a fixed center
highlight on the right; the optional information callback supplies wrapped text
on the left. The select callback runs on Cross with the highlighted index.
Call `close()` after accepting a selection if desired. Callbacks/context must
remain valid until the tab is removed. Tabs size to their labels and scroll to
keep the active tab visible.

### Popups

`setProgress(label, current, total)` captures input and displays progress. A
nonpositive total means discovery is still in progress. Pass `NULL` for the
label to dismiss it, including during deactivation.

`setModal(draw)` uses a `ModMenuModal` callback receiving newly pressed buttons.
Draw content with `drawBox` and `drawText`; return zero to dismiss. The owner
must clear its callback with `setModal(NULL)` during deactivation. The callback
is retained, not copied. See [bios-warning](../bios-warning/main.c) for an example.

Persistence belongs to the loader and works without a menu. New mods should use
`moduleConfigField_t` and loader `loadConfig` / `saveConfig` directly; see
[CONFIG.md](../../docs/CONFIG.md).

## Implementation and verification

`main.c` owns registration and UI; `backend.c` handles rendering and input.
Checked-in tables identify supported overlays, with runtime signature guards.
Campaign adds a pause-menu row; multiplayer replaces Continue with MOD MENU.
Input is captured before gameplay receives buttons/sticks and restored on close.

Offline campaign and Local Play navigation/input capture have passed user tests.
PAL testing also covered the new loader hook and in-game menu after map hops.
This does not qualify every level, split-screen setup, or online configuration.
See [ADDRESSES.md](../../docs/ADDRESSES.md) for regional evidence and online limits.
