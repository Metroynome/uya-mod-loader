#include "module.h"
#include "modules/modmenu/include/modmenu.h"
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/uya.h>
#include "training/training.h"

#define HORIZON_ARENA_START (0x000c8000)

void moduleUpdate(void);

extern int SimPlayerCount;

static const moduleApi_t* loader;
static const modMenuApi_t* menu;
static int tab;
/* These belong to the module, not the level. No configuration file is needed. */
static int count;
static int behavior;
static int lastTime = -1;
static int lastMap = -1;
static int loadTime = -1;
static int running;

static const char* behaviorLabel(void* context, int index)
{
	static const char* names[] = {"Idle", "Passive", "Aggressive"};
	(void)context;
	return index >= 0 && index < 3 ? names[index] : "Idle";
}

static void resetGame(void)
{
	botsReset(loader->mode == UYA_MODE_MULTIPLAYER);
	running = 0;
	lastTime = lastMap = loadTime = -1;
}

static void deactivate(void)
{
	resetGame();
	/* The provider may be inactive online. Keep its tab handle until it
	 * returns instead of calling inactive code or registering duplicate items. */
	if (menu && menu == modmenuFind(loader) && tab > 0) {
		menu->removeTab(tab);
		menu = 0;
		tab = 0;
	}
}

static void registerMenu(void)
{
	static const modMenuItem_t amount = {"Bots", MODMENU_RANGE, &count, 0, TRAINING_MAX_BOTS, 1, 0, 0};
	static const modMenuItem_t style = {"Behavior", MODMENU_RANGE, &behavior, 0, 2, 1, 0, 0};
	static const modMenuItem_t help = {"Bot count applies on the next game or reload", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	static const modMenuItem_t online = {"Online currently supports up to 3 bots", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	const modMenuApi_t* current = modmenuFind(loader);
	if (!current)
		return;
	if (current != menu) {
		menu = current;
		tab = 0;
	}
	if (!menu || tab > 0)
		return;
	tab = menu->addTab("bots", "Bots", UYA_MODE_MULTIPLAYER);
	if (tab <= 0)
		return;
	menu->setTabContexts(tab, UYA_CONTEXT_LOCAL | UYA_CONTEXT_ONLINE);
	menu->addItem(tab, &amount);
	menu->setItemValueLabel(menu->addItem(tab, &style), behaviorLabel);
	menu->addItem(tab, &help);
	menu->addItem(tab, &online);
}

static int onlineCompatible(void)
{
	const volatile moduleTable_t* table = UYA_MODULE_TABLE;
	u32 i, address = (u32)moduleUpdate;
	if (table->magic != UYA_MODULE_TABLE_MAGIC || table->count > UYA_MODULE_LIMIT)
		return 0;
	for (i = 0; i < table->count; ++i) {
		const volatile moduleInfo_t* entry = &table->entries[i];
		if (address >= entry->address && address - entry->address < entry->size)
			return entry->address >= 0x02000000 ||
			       (entry->address < HORIZON_ARENA_START && entry->size <= HORIZON_ARENA_START - entry->address);
	}
	return 0;
}

int moduleInit(const moduleApi_t* api)
{
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	loader = api;
	/* Keep the complete image outside Horizon's low-memory reservations. */
	api->restrictContexts(onlineCompatible() ? UYA_CONTEXT_LOCAL | UYA_CONTEXT_ONLINE : UYA_CONTEXT_LOCAL);
	api->setLifecycle(0, deactivate);
	return 0;
}

void moduleUpdate(void)
{
	GameSettings* settings;
	int time, map;
	BotsOnline = loader->context == UYA_CONTEXT_ONLINE;
	registerMenu();
	settings = gameGetSettings();
	if (!settings || !isInGame()) {
		if (running)
			resetGame();
		return;
	}
	/* Only the host creates training players. The original code is local AI,
	 * not a protocol for injecting additional remote clients. */
	if (loader->context == UYA_CONTEXT_ONLINE && !gameAmIHost())
		return;
	map = gameGetCurrentMapId();
	if (running && (map != lastMap || settings->GameLoadStartTime != loadTime || botsLevelChanged()))
		resetGame();
	if (!running) {
		SimPlayerCount = count;
		if (BotsOnline && SimPlayerCount > 3)
			SimPlayerCount = 3;
		lastMap = map;
		loadTime = settings->GameLoadStartTime;
		running = 1;
	}
	State.AggroMode = behavior == 0   ? TRAINING_AGGRESSION_IDLE
	                  : behavior == 1 ? TRAINING_AGGRESSION_PASSIVE
	                                  : TRAINING_AGGRESSION_AGGRO;
	time = gameGetTime();
	if (time == lastTime || !SimPlayerCount)
		return;
	lastTime = time;
	uyaPreUpdate();
	startBots();
	uyaPostUpdate();
}
