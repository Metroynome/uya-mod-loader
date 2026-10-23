/* Development-only Local Play controls. Map Loader owns all transition work. */
#include "include/forge.h"
#include "include/modmenu.h"

void forgeBridgeInit(void);
void forgeBridgeDisable(void);
void forgeBridgeTick(const MapHopApi_t*);
static int active(void);

static const ModuleApi_t* loader;
static const ModMenuApi_t* menu;
static int enabled, tab, item, refreshItem, lastReady = -1, lastRefresh = -1;
static const ForgeApi_t service = {FORGE_API_VERSION, sizeof(ForgeApi_t), active};

static int active(void)
{
	return enabled && loader->context == UYA_CONTEXT_LOCAL;
}

static const MapHopApi_t* hop(void)
{
	const MapHopApi_t* p = loader->findService(MAP_HOP_SERVICE_ID, FORGE_API_VERSION);
	return p && p->version == FORGE_API_VERSION && p->size >= sizeof(*p) ? p : 0;
}

static void reload(void* unused)
{
	const MapHopApi_t* p = hop();
	(void)unused;
	if (p && p->reload() > 0 && menu)
		menu->close();
}

static void refresh(void* unused)
{
	const MapHopApi_t* p = hop();
	(void)unused;
	if (p && !p->request(FORGE_REFRESH, 0) && menu)
		menu->close();
}

static void activate(void)
{
	forgeBridgeDisable();
	enabled = 1;
}

static void deactivate(void)
{
	enabled = 0;
	forgeBridgeDisable();
	if (menu && tab > 0)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	lastReady = -1;
}

int moduleInit(const ModuleApi_t* api)
{
	loader = api;
	forgeBridgeInit();
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	api->restrictContexts(UYA_CONTEXT_LOCAL);
	api->setLifecycle(activate, deactivate);
	enabled = 1;
	return api->publishService(FORGE_SERVICE_ID, FORGE_API_VERSION, &service);
}

void moduleUpdate(void)
{
	const MapHopApi_t* p = hop();
	int ready = p && p->available();
	static const ModMenuItem_t action = {"Reload Current Map", MODMENU_ACTION, 0, 0, 0, 0, reload, 0};
	static const ModMenuItem_t refreshAction = {"Refresh Custom Maps", MODMENU_ACTION, 0, 0, 0, 0, refresh, 0};
	int canRefresh = p && p->context() && p->state() != FORGE_SCANNING && p->state() != FORGE_LOADING;
	forgeBridgeTick(p);
	if (menu && p && (p->state() == FORGE_SCANNING || p->state() == FORGE_LOADING))
		menu->close();
	if (!menu)
		menu = modmenuFind(loader);
	if (!menu)
		return;
	if (tab <= 0) {
		tab = menu->addTab("forge", "Forge", UYA_MODE_MULTIPLAYER);
		if (tab <= 0)
			return;
		menu->setTabContexts(tab, UYA_CONTEXT_LOCAL);
		item = menu->addItem(tab, &action);
		refreshItem = menu->addItem(tab, &refreshAction);
		lastReady = lastRefresh = -1;
	}
	if (lastRefresh != canRefresh) {
		menu->setItemEnabled(refreshItem, canRefresh);
		lastRefresh = canRefresh;
	}
	if (lastReady != ready) {
		menu->setItemEnabled(item, ready);
		lastReady = ready;
	}
}

void* memset(void* dest, int value, u32 size)
{
	u8* p = dest;
	while (size--)
		*p++ = (u8)value;
	return dest;
}
