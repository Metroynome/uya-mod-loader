/* Offline menu core. Other modules own gameplay behavior; this module owns
 * registration, selection, input and drawing. No Horizon/server state is used. */
#include "modules/modmenu/include/modmenu.h"
#include "backend.h"
#include "version.h"

typedef struct tab {
	int handle;
	u32 modes, contexts, executionContexts;
	char key[32], label[32];
	int count, cursor, firstRow;
	ModMenuListLabel listLabel, listInfo;
	ModMenuListSelect listSelect;
	void* context;
	char actionLabel[24];
	ModMenuCallback action;
	void* actionContext;
} tab_t;

typedef struct item {
	int handle, tab, enabled;
	char label[48];
	modMenuItem_t definition;
	ModMenuListLabel valueLabel;
} item_t;

static int setTabVisibility(int tab, u32 contexts);
static int setTabContexts(int tab, u32 contexts);
static int addTab(const char* key, const char* label, u32 modes);
static int addItem(int tab, const modMenuItem_t* d);
static int removeTab(int tab);
static int setItemEnabled(int handle, int enabled);
static int setItemValueLabel(int handle, ModMenuListLabel label);
static int isOpen(void);
static void closeMenu(void);
static int getActiveTab(void);
static int focusTab(int tab);
static int setList(int tab, int count, ModMenuListLabel label, ModMenuListSelect select, void* context, int initial);
static int setListInfo(int tab, ModMenuListLabel info);
static int menuConfigLoad(const char* mod, const modMenuConfigField_t* f, int n);
static int menuConfigSave(const char* mod, const modMenuConfigField_t* f, int n);
static void setProgress(const char* label, int current, int total);
static int setModal(ModMenuModal draw);
static int setTabAction(int tab, const char* label, ModMenuCallback callback, void* context);

static tab_t tabs[MODMENU_MAX_TABS];
static item_t items[MODMENU_MAX_ITEMS];
static const moduleApi_t* loader;
static int homeTab;
static char progressLabel[64];
static int progressCurrent, progressTotal, progressPulse;
static int nextHandle = 1, opened, selectedTab, selectedItem, lastLevel = -999;
static u32 previousButtons, previousMode, closingButtons;
static const modMenuApi_t menuApi = {
    MODMENU_API_VERSION, sizeof(modMenuApi_t), addTab,      addItem,  removeTab,      setItemEnabled,   isOpen,
    closeMenu,           getActiveTab,       focusTab,    setList,  setListInfo,    setTabVisibility, setTabContexts,
    menuConfigLoad,      menuConfigSave,     setProgress, setModal, menuBackendBox, menuBackendText, setTabAction,
    setItemValueLabel};
/* A window of whole tabs: measured text plus Horizon-style padding. Scroll
 * the first visible tab only when the focused tab would fall outside it. */
static int firstTab;
static ModMenuModal modal;
static int modalStarted;

static int copyText(char* dst, const char* src, u32 cap)
{
	u32 i = 0;
	if (!src || !*src)
		return 0;
	while (src[i] && i + 1 < cap) {
		dst[i] = src[i];
		++i;
	}
	dst[i] = 0;
	return !src[i];
}

static int equal(const char* a, const char* b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

static int tabIndex(int handle)
{
	int i;
	for (i = 0; i < MODMENU_MAX_TABS; ++i)
		if (tabs[i].handle == handle && handle)
			return i;
	return -1;
}

static int visible(int tab)
{
	int i = tabIndex(tab), level = menuBackendLevel();
	u32 context = loader->mode == UYA_MODE_MULTIPLAYER
	                           ? (level == 39 ? MODMENU_SHOW_MAIN_MENU : MODMENU_SHOW_MULTIPLAYER)
	                           : (level < 0 ? MODMENU_SHOW_MAIN_MENU : MODMENU_SHOW_CAMPAIGN);
	return i >= 0 && (tabs[i].modes & loader->mode) && (tabs[i].contexts & context) &&
	       (tabs[i].executionContexts & loader->context);
}

static int setTabVisibility(int tab, u32 contexts)
{
	int i = tabIndex(tab);
	if (i < 0 || (contexts & ~MODMENU_SHOW_EVERYWHERE))
		return -1;
	tabs[i].contexts = contexts;
	tabs[i].modes = UYA_MODE_BOTH;
	return 0;
}

static int setTabContexts(int tab, u32 contexts)
{
	int i = tabIndex(tab);
	if (i < 0 || (contexts & ~UYA_CONTEXT_ALL))
		return -1;
	tabs[i].executionContexts = contexts;
	return 0;
}

static int addTab(const char* key, const char* label, u32 modes)
{
	int i, freeSlot = -1;
	char k[32], l[32];
	if (!copyText(k, key, sizeof(k)) || !copyText(l, label, sizeof(l)) || !modes || (modes & ~3U))
		return -1;
	for (i = 0; i < MODMENU_MAX_TABS; ++i) {
		if (tabs[i].handle && equal(tabs[i].key, k))
			return -1;
		if (!tabs[i].handle && freeSlot < 0)
			freeSlot = i;
	}
	if (freeSlot < 0 || nextHandle == 0x7fffffff)
		return -1;
	tabs[freeSlot].handle = nextHandle++;
	tabs[freeSlot].modes = modes;
	tabs[freeSlot].firstRow = 0;
	tabs[freeSlot].contexts = MODMENU_SHOW_EVERYWHERE;
	tabs[freeSlot].executionContexts = UYA_CONTEXT_ALL;
	copyText(tabs[freeSlot].key, k, 32);
	copyText(tabs[freeSlot].label, l, 32);
	return tabs[freeSlot].handle;
}

static int addItem(int tab, const modMenuItem_t* d)
{
	int i;
	char label[48];
	if (tabIndex(tab) < 0 || !d || !copyText(label, d->label, sizeof(label)))
		return -1;
	if (d->type < MODMENU_ACTION || d->type > MODMENU_HEADER)
		return -1;
	if (d->type == MODMENU_ACTION && !d->callback)
		return -1;
	if ((d->type == MODMENU_TOGGLE || d->type == MODMENU_RANGE) && !d->value)
		return -1;
	if (d->type == MODMENU_RANGE && (d->minimum > d->maximum || d->step <= 0))
		return -1;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (!items[i].handle) {
			if (nextHandle == 0x7fffffff)
				return -1;
			items[i].handle = nextHandle++;
			items[i].tab = tab;
			items[i].enabled = 1;
			items[i].definition = *d;
			items[i].valueLabel = 0;
			copyText(items[i].label, label, 48);
			items[i].definition.label = items[i].label;
			return items[i].handle;
		}
	return -1;
}

static int removeTab(int tab)
{
	int i, index = tabIndex(tab);
	if (index < 0)
		return -1;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].tab == tab)
			items[i].handle = 0;
	tabs[index].handle = 0;
	tabs[index].count = 0;
	tabs[index].listInfo = 0;
	tabs[index].action = 0;
	tabs[index].actionContext = 0;
	tabs[index].actionLabel[0] = 0;
	if (selectedTab == tab) {
		selectedTab = 0;
		selectedItem = 0;
	}
	return 0;
}

static int setItemEnabled(int handle, int enabled)
{
	int i;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].handle == handle && handle) {
			items[i].enabled = !!enabled;
			return 0;
		}
	return -1;
}

static int setItemValueLabel(int handle, ModMenuListLabel label)
{
	int i;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].handle == handle && handle && items[i].definition.type == MODMENU_RANGE) {
			items[i].valueLabel = label;
			return 0;
		}
	return -1;
}

static int isOpen(void)
{
	return opened;
}

static void closeMenu(void)
{
	opened = 0;
	closingButtons = previousButtons;
}

static int getActiveTab(void)
{
	return selectedTab;
}

static int focusTab(int tab)
{
	if (!visible(tab))
		return -1;
	selectedTab = tab;
	selectedItem = 0;
	return 0;
}

static int setList(int tab, int count, ModMenuListLabel label, ModMenuListSelect select, void* context, int initial)
{
	int i = tabIndex(tab);
	if (i < 0 || count < 0 || (count && (!label || !select)))
		return -1;
	tabs[i].count = count;
	tabs[i].listLabel = label;
	tabs[i].listSelect = select;
	tabs[i].context = context;
	tabs[i].cursor = initial >= 0 && initial < count ? initial : 0;
	return 0;
}

static int setListInfo(int tab, ModMenuListLabel info)
{
	int i = tabIndex(tab);
	if (i < 0)
		return -1;
	tabs[i].listInfo = info;
	return 0;
}

static int setTabAction(int tab, const char* label, ModMenuCallback callback, void* context)
{
	int i = tabIndex(tab);
	if (i < 0 || (callback && !copyText(tabs[i].actionLabel, label, sizeof(tabs[i].actionLabel))))
		return -1;
	tabs[i].action = callback;
	tabs[i].actionContext = context;
	return 0;
}

static void drawFooter(void)
{
	int i = tabIndex(selectedTab);
	menuBackendText(.12f, .81f, 0x8069cbf2, "\x14 \x15 TAB   \x10 SELECT   \x12 BACK");
	if (i >= 0 && tabs[i].action) {
		menuBackendText(.66f, .81f, 0x8069cbf2, "\x13");
		menuBackendText(.69f, .81f, 0x8069cbf2, tabs[i].actionLabel);
	}
}

/* Retain the original menu API as forwarding helpers for existing clients. */
static int menuConfigLoad(const char* mod, const modMenuConfigField_t* f, int n)
{
	return loader->loadConfig(mod, f, n);
}

static int menuConfigSave(const char* mod, const modMenuConfigField_t* f, int n)
{
	return loader->saveConfig(mod, f, n);
}

static void setProgress(const char* label, int current, int total)
{
	if (!label) {
		progressLabel[0] = 0;
		closingButtons = previousButtons;
		return;
	}
	copyText(progressLabel, label, sizeof(progressLabel));
	progressCurrent = current;
	progressTotal = total;
}

static void drawProgress(void)
{
	float portion = progressTotal > 0 ? (float)progressCurrent / progressTotal : 0;
	float x = .3f;
	if (portion < 0)
		portion = 0;
	if (portion > 1)
		portion = 1;
	menuBackendBox(0, 0, 1, 1, 0x50000000);
	menuBackendBox(.24f, .36f, .52f, .25f, 0x8018608f);
	menuBackendBox(.25f, .37f, .5f, .23f, 0x80123251);
	menuBackendText(.28f, .43f, 0x8069cbf2, progressLabel);
	menuBackendBox(.3f, .52f, .4f, .025f, 0x8004223f);
	if (progressTotal <= 0) {
		x += ((progressPulse++ % 80) / 80.0f) * .30f;
		portion = .25f;
	}
	menuBackendBox(x, .52f, .4f * portion, .025f, 0x803880af);
}

/* The owning module supplies the content and dismisses on deactivation. */
static int setModal(ModMenuModal draw)
{
	if (draw && modal && modal != draw)
		return 0;
	modal = draw;
	modalStarted = 0;
	return 1;
}

static void selectTab(int direction)
{
	int n, index = tabIndex(selectedTab);
	if (index < 0)
		index = direction > 0 ? -1 : 0;
	for (n = 0; n < MODMENU_MAX_TABS; ++n) {
		index = (index + direction + MODMENU_MAX_TABS) % MODMENU_MAX_TABS;
		if (tabs[index].handle && visible(tabs[index].handle)) {
			selectedTab = tabs[index].handle;
			selectedItem = 0;
			return;
		}
	}
	selectedTab = 0;
	selectedItem = 0;
}

static void selectItem(int direction)
{
	int i, index = tabIndex(selectedTab);
	if (index >= 0 && tabs[index].count) {
		tab_t* t = &tabs[index];
		t->cursor = (t->cursor + direction + t->count) % t->count;
		return;
	}
	index = -1;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].handle == selectedItem && selectedItem)
			index = i;
	if (index < 0)
		index = direction > 0 ? -1 : 0;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i) {
		index = (index + direction + MODMENU_MAX_ITEMS) % MODMENU_MAX_ITEMS;
		if (items[index].handle && items[index].tab == selectedTab && items[index].enabled &&
		    items[index].definition.type != MODMENU_LABEL && items[index].definition.type != MODMENU_HEADER) {
			selectedItem = items[index].handle;
			return;
		}
	}
	selectedItem = 0;
}

static void activate(int direction)
{
	int i = tabIndex(selectedTab);
	ModMenuCallback callback;
	void* context;
	if (i >= 0 && tabs[i].count) {
		if (!direction)
			tabs[i].listSelect(tabs[i].context, tabs[i].cursor);
		return;
	}
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].handle == selectedItem && selectedItem) {
			modMenuItem_t* d = &items[i].definition;
			int value;
			if (!items[i].enabled || !visible(items[i].tab))
				return;
			callback = d->callback;
			context = d->context;
			if (d->type == MODMENU_TOGGLE)
				*d->value = !*d->value;
			else if (d->type == MODMENU_RANGE) {
				long long next = (long long)*d->value + (direction < 0 ? -(long long)d->step : d->step);
				if (items[i].valueLabel)
					value = next < d->minimum ? d->maximum : next > d->maximum ? d->minimum : (int)next;
				else
					value = next < d->minimum ? d->minimum : next > d->maximum ? d->maximum : (int)next;
				*d->value = value;
			} else if (d->type != MODMENU_ACTION || direction < 0)
				return;
			/* Copy callback/context first: callbacks may unregister their own tab. */
			if (callback)
				callback(context);
			return;
		}
}

static void number(char* dst, int value)
{
	char rev[12];
	u32 n = value < 0 ? 0U - (u32)value : (u32)value;
	int i = 0, j = 0;
	if (value < 0)
		dst[j++] = '-';
	do {
		rev[i++] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	while (i)
		dst[j++] = rev[--i];
	dst[j] = 0;
}

static float tabWidth(int index)
{
	float w = menuBackendWidth(tabs[index].label) + .04f;
	return w > .70f ? .70f : w;
}

static void drawTabs(void)
{
	int ids[MODMENU_MAX_TABS], n = 0, k, selected = 0, end;
	float used, x;
	for (k = 0; k < MODMENU_MAX_TABS; ++k)
		if (tabs[k].handle && visible(tabs[k].handle)) {
			if (tabs[k].handle == selectedTab)
				selected = n;
			ids[n++] = k;
		}
	if (firstTab >= n || selected < firstTab)
		firstTab = selected;
	for (;;) {
		used = 0;
		for (k = firstTab; k <= selected && k < n; ++k)
			used += tabWidth(ids[k]);
		if (used <= (firstTab > 0 ? .72f : .76f) || firstTab == selected)
			break;
		++firstTab;
	}
	x = firstTab > 0 ? .14f : .1f;
	end = firstTab;
	for (k = firstTab; k < n; ++k) {
		float w = tabWidth(ids[k]);
		if (x + w > .8601f)
			break;
		menuBackendBox(x + .003f, .225f, w - .006f, .075f, ids[k] == tabIndex(selectedTab) ? 0x803880af : 0x8018608f);
		menuBackendText(x + .02f, .245f, 0x8069cbf2, tabs[ids[k]].label);
		x += w;
		end = k + 1;
	}
	if (firstTab > 0)
		menuBackendText(.11f, .245f, 0x8069cbf2, "<");
	if (end < n)
		menuBackendText(.87f, .245f, 0x8069cbf2, ">");
}

/* Wrap provider descriptions to the left pane using actual glyph widths. */
static void drawInfo(const char* text)
{
	char line[96];
	int n, lastSpace;
	float y = .325f;
	if (!text)
		return;
	while (*text && y < .775f) {
		n = 0;
		lastSpace = -1;
		while (text[n] && text[n] != '\n' && n < 94) {
			line[n] = text[n];
			line[n + 1] = 0;
			if (n && menuBackendWidth(line) > .335f)
				break;
			if (text[n] == ' ')
				lastSpace = n;
			++n;
		}
		if (text[n] && text[n] != '\n' && lastSpace > 0)
			n = lastSpace;
		if (!n && *text != '\n')
			n = 1;
		{
			int j;
			for (j = 0; j < n; ++j)
				line[j] = text[j];
			line[n] = 0;
		}
		menuBackendText(.125f, y, 0x8069cbf2, line);
		y += .038f;
		text += n;
		if (*text == '\n')
			++text;
		while (*text == ' ')
			++text;
	}
}

static void draw(void)
{
	int i, index = tabIndex(selectedTab), row = 0, selectedRow = 0, start = 0;
	char value[16];
	/* Horizon config.c frame proportions and palette; retain the full-screen fade. */
	menuBackendBox(0, 0, 1, 1, 0x50000000);
	menuBackendBox(.1f, .15f, .8f, .7f, 0x8018608f);
	menuBackendBox(.11f, .3f, .78f, .5f, 0x80123251);
	menuBackendBox(.1f, .225f, .8f, .075f, 0x8004223f);
	menuBackendText(.36f, .17f, 0x8069cbf2, "Mod Config Menu");
	drawTabs();
	if (selectedTab == homeTab) {
		const char* region = loader->region == 1   ? "Region: NTSC-U"
		                     : loader->region == 2 ? "Region: PAL"
		                                           : "Region: NTSC-J";
		menuBackendText(.13f, .325f, 0x8069cbf2, "UYA Mod Menu   v" MODMENU_VERSION_STRING);
		menuBackendText(.13f, .39f, 0x8069cbf2, "Shared controls for campaign and multiplayer.");
		menuBackendText(.13f, .435f, 0x8069cbf2, "Mods add their own tabs, settings and actions.");
		menuBackendBox(.125f, .50f, .75f, .12f, 0x800c2241);
		menuBackendText(
		    .14f, .52f, 0x8069cbf2, loader->mode == UYA_MODE_CAMPAIGN ? "Mode: Campaign" : "Mode: Multiplayer");
		menuBackendText(.53f, .52f, 0x8069cbf2, region);
		menuBackendText(.14f, .575f, 0x8069cbf2, "Menu works without a server connection.");
		menuBackendText(.13f, .66f, 0x8069cbf2, "D-pad: Browse settings / change values");
		menuBackendText(.13f, .715f, 0x8069cbf2, "Enable mods in modules.txt, then restart.");
		drawFooter();
		return;
	}
	if (index >= 0 && tabs[index].count) {
		tab_t* t = &tabs[index];
		/* Odd row count keeps the highlighted entry in the exact center.
         * Wrap neighbors at both ends, as Horizon's vertical list does. */
		menuBackendBox(.11f, .3f, .365f, .5f, 0x800c2241);
		menuBackendBox(.485f, .515f, .40f, .048f, 0x8018608f);
		for (i = -4; i <= 4; ++i) {
			int item = (t->cursor + i + t->count * 4) % t->count;
			const char* label = t->listLabel(t->context, item);
			float x = .875f - menuBackendWidth(label);
			/* Long names stay inside their pane; the game clips neither text
             * nor tabs, so truncate only the drawn copy when necessary. */
			char shortLabel[48];
			int n = 0;
			if (x < .495f) {
				while (label[n] && n < 44) {
					shortLabel[n] = label[n];
					shortLabel[n + 1] = 0;
					if (menuBackendWidth(shortLabel) > .35f)
						break;
					++n;
				}
				shortLabel[n++] = '.';
				shortLabel[n++] = '.';
				shortLabel[n] = 0;
				label = shortLabel;
				x = .875f - menuBackendWidth(label);
			}
			menuBackendText(x, .525f + i * .05f, 0x8069cbf2, label);
		}
		if (t->listInfo)
			drawInfo(t->listInfo(t->context, t->cursor));
		drawFooter();
		return;
	}
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].handle && items[i].tab == selectedTab) {
			if (items[i].handle == selectedItem)
				selectedRow = row;
			++row;
		}
	/* Keep the viewport still until selection crosses either visible edge. */
	if (index >= 0) {
		if (selectedRow < tabs[index].firstRow)
			tabs[index].firstRow = selectedRow;
		else if (selectedRow >= tabs[index].firstRow + 8)
			tabs[index].firstRow = selectedRow - 7;
		start = tabs[index].firstRow;
	}
	row = 0;
	for (i = 0; i < MODMENU_MAX_ITEMS; ++i)
		if (items[i].handle && items[i].tab == selectedTab) {
			float y;
			u32 color = items[i].enabled ? 0x8069cbf2 : 0x80606060;
			if (row++ < start)
				continue;
			if (row > start + 8)
				break;
			y = .32f + (row - start - 1) * .058f;
			if (items[i].handle == selectedItem) {
				color = 0x8069cbf2;
				/* Keep standard controls inside the same .11..89 content pane
                 * used by the home tab and custom-map list. */
				menuBackendBox(.115f, y - .008f, .77f, .052f, 0x8018608f);
			}
			if (items[i].definition.type == MODMENU_HEADER) {
				/* Horizon's section labels are centered and nonselectable. */
				menuBackendText(.5f - menuBackendWidth(items[i].label) * .5f, y, 0x8069cbf2, items[i].label);
				continue;
			}
			if (items[i].definition.type == MODMENU_ACTION) {
				/* Horizon config.c button: inset dark border, gray face, centered
                 * gold label. The outer row highlight still indicates focus. */
				int enabled = items[i].enabled;
				menuBackendBox(.165f, y - .006f, .67f, .048f, enabled ? 0x80303030 : 0x40181818);
				menuBackendBox(.17f, y - .001f, .66f, .038f, enabled ? 0x80505050 : 0x40282828);
				menuBackendText(
				    .5f - menuBackendWidth(items[i].label) * .5f, y, enabled ? 0x8069cbf2 : 0x40346579, items[i].label);
				continue;
			}
			menuBackendText(.15f, y, color, items[i].label);
			if (items[i].definition.type == MODMENU_TOGGLE) {
				const char* state = *items[i].definition.value ? "ON" : "OFF";
				menuBackendText(.865f - menuBackendWidth(state), y, color, state);
			}
			if (items[i].definition.type == MODMENU_RANGE) {
				const char* label = items[i].valueLabel ?
				    items[i].valueLabel(items[i].definition.context, *items[i].definition.value) : 0;
				if (!label) {
					number(value, *items[i].definition.value);
					label = value;
				}
				menuBackendText(.865f - menuBackendWidth(label), y, color, label);
			}
		}
	drawFooter();
}

static void deactivate(void)
{
	progressLabel[0] = 0;
	closeMenu();
	menuBackendDeactivate();
	closingButtons = previousButtons = 0;
	lastLevel = -999;
}

int moduleInit(const moduleApi_t* api)
{

	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	loader = api;

	loader->setLifecycle(0, deactivate);
	homeTab = addTab("modmenu.home", "Mod Menu", UYA_MODE_BOTH);
	if (homeTab < 0)
		return -1;
	if (loader->publishService(MODMENU_SERVICE_ID, MODMENU_API_VERSION, &menuApi) < 0)
		return -1;
	loader->print("modmenu: ready; L3 + R3 opens the menu\n");
	return 0;
}

static void openFromPause(void)
{
	opened = 1;
	closingButtons = 0;
	/* Consume the X press that chose this pause entry; do not also activate
     * whichever mod action happened to be focused last time. */
	previousButtons = menuBackendButtons(loader->mode);
}

void moduleUpdate(void)
{
	u32 buttons, pressed;
	int level;
	if (!menuBackendReady(loader->mode)) {
		closeMenu();
		menuBackendRelease();
		closingButtons = 0;
		previousButtons = 0;
		lastLevel = -999;
		return;
	}
	level = menuBackendLevel();
	buttons = menuBackendButtons(loader->mode);
	if (previousMode != loader->mode || level != lastLevel) {
		closeMenu();
		menuBackendRelease();
		closingButtons = 0;
		previousButtons = buttons;
		previousMode = loader->mode;
		lastLevel = level;
	}
	menuBackendPause(loader->mode, openFromPause);
	if (modal && (modalStarted || menuBackendPopupReady(loader->mode))) {
		modalStarted = 1;
		menuBackendCapture(loader->mode);
		if (!modal(buttons & ~previousButtons)) {
			modal = 0;
			closingButtons = buttons;
		}
		previousButtons = buttons;
		return;
	}
	if (progressLabel[0]) {
		menuBackendCapture(loader->mode);
		previousButtons = buttons;
		drawProgress();
		return;
	}
	pressed = buttons & ~previousButtons;
	/* Thumbstick shortcuts belong to front-end menus, not gameplay.
     * In-game opening is exclusively through the pause-menu callback. */
	if ((loader->mode == UYA_MODE_MULTIPLAYER ? level == 39 : level < 0) && (buttons & 6) == 6 &&
	    (previousButtons & 6) != 6) {
		opened = !opened;
		if (!opened)
			closingButtons = buttons;
	}
	previousButtons = buttons;
	if (!opened) {
		if (buttons & closingButtons)
			menuBackendCapture(loader->mode);
		else {
			closingButtons = 0;
			menuBackendRelease();
		}
		return;
	}
	menuBackendCapture(loader->mode);
	if (pressed & 0x1000) {
		opened = 0;
		closingButtons = buttons;
		return;
	}
	if (!visible(selectedTab))
		selectTab(1);
	if (pressed & 0x400)
		selectTab(-1);
	else if (pressed & 0x800)
		selectTab(1);
	/* Handle shortcuts only for the visible, focused tab and once per press.
	 * Return after callbacks: they may rebuild tabs or open a progress popup. */
	if (pressed & 0x8000) {
		int i = tabIndex(selectedTab);
		if (i >= 0 && visible(selectedTab) && tabs[i].action) {
			tabs[i].action(tabs[i].actionContext);
			if (opened && !progressLabel[0])
				draw();
			return;
		}
	}
	if (!selectedItem && (tabIndex(selectedTab) < 0 || !tabs[tabIndex(selectedTab)].count))
		selectItem(1);
	if (pressed & 0x10)
		selectItem(-1);
	else if (pressed & 0x40)
		selectItem(1);
	if (pressed & 0x80)
		activate(-1);
	else if (pressed & 0x20)
		activate(1);
	else if (pressed & 0x4000)
		activate(0);
	if (opened)
		draw();
}

/* GCC may lower aggregate initialization to memset even in freestanding code. */
void* memset(void* dst, int value, u32 size)
{
	u8* p = dst;
	while (size--)
		*p++ = (u8)value;
	return dst;
}
