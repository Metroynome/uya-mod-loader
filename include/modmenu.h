#ifndef UYA_MODMENU_H
#define UYA_MODMENU_H
#include "module.h"
#define MODMENU_SERVICE_ID 0x4d454e55U
#define MODMENU_API_VERSION 1U
#define MODMENU_ACTION 1
#define MODMENU_TOGGLE 2
#define MODMENU_RANGE 3
#define MODMENU_LABEL 4
#define MODMENU_SHOW_MAIN_MENU 1U
#define MODMENU_SHOW_CAMPAIGN 2U
#define MODMENU_SHOW_MULTIPLAYER 4U
#define MODMENU_SHOW_EVERYWHERE 7U
#define MODMENU_MAX_TABS 8
#define MODMENU_MAX_ITEMS 40
/* IDs are opaque positive handles; negative return values indicate failure.
 * Labels are copied. Value/context pointers must remain valid until removeTab.
 * Callbacks run synchronously on the game thread. Never block inside one. */
typedef void (*ModMenuCallback)(void *context);
typedef struct ModMenuItem {
    const char *label;
    unsigned int type;
    int *value;
    int minimum, maximum, step;
    ModMenuCallback callback;
    void *context;
} ModMenuItem;
/* Compatibility aliases; persistence is owned by the loader. */
typedef ModuleConfigField ModMenuConfigField;
#define MODMENU_CONFIG_MAX_FIELDS UYA_CONFIG_MAX_FIELDS
#define MODMENU_CONFIG_OK UYA_CONFIG_OK
#define MODMENU_CONFIG_MISSING UYA_CONFIG_MISSING
#define MODMENU_CONFIG_INVALID UYA_CONFIG_INVALID
#define MODMENU_CONFIG_IO UYA_CONFIG_IO

typedef const char *(*ModMenuListLabel)(void *context, int index);
typedef void (*ModMenuListSelect)(void *context, int index);
typedef struct ModMenuApi {
    unsigned int version, size;
    int (*addTab)(const char *key, const char *label, unsigned int modes);
    int (*addItem)(int tab, const ModMenuItem *item);
    int (*removeTab)(int tab);
    int (*setItemEnabled)(int item, int enabled);
    int (*isOpen)(void);
    void (*close)(void);
    /* Appended API-1 fields; size guards callers using this extension. */
    int (*getActiveTab)(void);
    int (*focusTab)(int tab);
    /* Virtual list: entries do not consume item slots. X commits selection. */
    int (*setList)(int tab, int count, ModMenuListLabel label,
                   ModMenuListSelect select, void *context, int initial);
    /* Optional information for the highlighted row, displayed on the left. */
    int (*setListInfo)(int tab, ModMenuListLabel info);
    /* Contexts are independent of module execution modes; combine with |. */
    int (*setTabVisibility)(int tab, unsigned int contexts);
    /* API-1 extension: execution contexts ANDed with the existing screen
     * visibility filter. Thus LOCAL + MAIN_MENU means Local Play menus only. */
    int (*setTabContexts)(int tab, unsigned int contexts);
    /* host0:uya/config/<mod>.<region>.ini. Explicit calls, never per frame.
     * Save replaces the file; comments/unknown keys are not preserved. */
    int (*loadConfig)(const char *mod, const ModMenuConfigField *fields, int count);
    int (*saveConfig)(const char *mod, const ModMenuConfigField *fields, int count);
    /* Modal progress overlay. total<=0 shows indeterminate discovery;
     * label=NULL dismisses it. Caller must dismiss on deactivation. */
    void (*setProgress)(const char *label,int current,int total);
} ModMenuApi;
/* Look this up again after mode changes; absence is normal, not fatal.
 * Retry registration from moduleUpdate until the provider is available. */
static const ModMenuApi *modmenuFind(const ModuleApi *loader)
{
    const ModMenuApi *menu;
    if (!loader || loader->version < 2 || !loader->findService) return 0;
    menu=(const ModMenuApi *)loader->findService(MODMENU_SERVICE_ID,MODMENU_API_VERSION);
    return menu && menu->version==MODMENU_API_VERSION && menu->size>=sizeof(*menu) ? menu : 0;
}
#endif
