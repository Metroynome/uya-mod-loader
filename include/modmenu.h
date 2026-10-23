#ifndef UYA_MODMENU_H
#define UYA_MODMENU_H

#include "module.h"

#define MODMENU_SERVICE_ID        (0x4d454e55)
#define MODMENU_API_VERSION       (1)
#define MODMENU_ACTION            (1)
#define MODMENU_TOGGLE            (2)
#define MODMENU_RANGE             (3)
#define MODMENU_LABEL             (4)
#define MODMENU_SHOW_MAIN_MENU    (1)
#define MODMENU_SHOW_CAMPAIGN     (2)
#define MODMENU_SHOW_MULTIPLAYER  (4)
#define MODMENU_SHOW_EVERYWHERE   (7)
#define MODMENU_MAX_TABS          (8)
#define MODMENU_MAX_ITEMS         (40)
#define MODMENU_CONFIG_MAX_FIELDS (UYA_CONFIG_MAX_FIELDS)
#define MODMENU_CONFIG_OK         (UYA_CONFIG_OK)
#define MODMENU_CONFIG_MISSING    (UYA_CONFIG_MISSING)
#define MODMENU_CONFIG_INVALID    (UYA_CONFIG_INVALID)
#define MODMENU_CONFIG_IO         (UYA_CONFIG_IO)

/* IDs are opaque positive handles; negative return values indicate failure.
 * Labels are copied. Value/context pointers must remain valid until removeTab.
 * Callbacks run synchronously on the game thread. Never block inside one. */
typedef void (*ModMenuCallback)(void* context);

typedef struct modMenuItem {
	const char* label;
	u32 type;
	int* value;
	int minimum, maximum, step;
	ModMenuCallback callback;
	void* context;
} modMenuItem_t;

/* Compatibility aliases; persistence is owned by the loader. */
typedef moduleConfigField_t modMenuConfigField_t;

typedef const char* (*ModMenuListLabel)(void* context, int index);
typedef void (*ModMenuListSelect)(void* context, int index);
/* Draw once per UI update; return zero to dismiss. Buttons are new presses. */
typedef int (*ModMenuModal)(u32 pressed);

typedef struct modMenuApi {
	u32 version, size;
	int (*addTab)(const char* key, const char* label, u32 modes);
	int (*addItem)(int tab, const modMenuItem_t* item);
	int (*removeTab)(int tab);
	int (*setItemEnabled)(int item, int enabled);
	int (*isOpen)(void);
	void (*close)(void);
	/* Appended API-1 fields; size guards callers using this extension. */
	int (*getActiveTab)(void);
	int (*focusTab)(int tab);
	/* Virtual list: entries do not consume item slots. X commits selection. */
	int (*setList)(int tab, int count, ModMenuListLabel label, ModMenuListSelect select, void* context, int initial);
	/* Optional information for the highlighted row, displayed on the left. */
	int (*setListInfo)(int tab, ModMenuListLabel info);
	/* Contexts are independent of module execution modes; combine with |. */
	int (*setTabVisibility)(int tab, u32 contexts);
	/* API-1 extension: execution contexts ANDed with the existing screen
     * visibility filter. Thus LOCAL + MAIN_MENU means Local Play menus only. */
	int (*setTabContexts)(int tab, u32 contexts);
	/* host0:uya/config/<mod>.<region>.ini. Explicit calls, never per frame.
     * Save replaces the file; comments/unknown keys are not preserved. */
	int (*loadConfig)(const char* mod, const modMenuConfigField_t* fields, int count);
	int (*saveConfig)(const char* mod, const modMenuConfigField_t* fields, int count);
	/* Modal progress overlay. total<=0 shows indeterminate discovery;
     * label=NULL dismisses it. Caller must dismiss on deactivation. */
	void (*setProgress)(const char* label, int current, int total);
	/* Optional module-owned popup. Returns zero if another popup owns it.
	 * Owner must clear it with NULL on deactivation; content is not copied. */
	int (*setModal)(ModMenuModal draw);
	void (*drawBox)(float x, float y, float width, float height, u32 color);
	void (*drawText)(float x, float y, u32 color, const char* text);
} modMenuApi_t;

/* Look this up again after mode changes; absence is normal, not fatal.
 * Retry registration from moduleUpdate until the provider is available. */
static const modMenuApi_t* modmenuFind(const moduleApi_t* loader)
{
	const modMenuApi_t* menu;
	if (!loader || loader->version < 2 || !loader->findService)
		return 0;
	menu = (const modMenuApi_t*)loader->findService(MODMENU_SERVICE_ID, MODMENU_API_VERSION);
	return menu && menu->version == MODMENU_API_VERSION && menu->size >= sizeof(*menu) ? menu : 0;
}
#endif
