#include "module.h"
#include "modules/modmenu/include/modmenu.h"
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/uya.h>
#include <libuya/stdio.h>
#include "training/training.h"

#define HORIZON_ARENA_START (0x000c8000)

void moduleUpdate(void);

extern int SimPlayerCount;
extern int Initialized;
extern simulatedPlayer_t SimPlayers[];

static void refreshBotList(void);

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
static int spawnItem;
static int pendingSpawn, pendingRemove;
static int listMask = -1;
static int botItems[TRAINING_MAX_BOTS];

static int canManageBots(void)
{
	return running && isInGame() && gameGetSettings() && (!BotsOnline || gameAmIHost());
}

static void spawnBot(void* context)
{
	(void)context;
	if (canManageBots())
		pendingSpawn = 1;
}

static void kickBot(void* context)
{
	if (canManageBots())
		pendingRemove = (int)context;
}

static void refreshBotList(void)
{
	int i, mask = 0, vacant = 0;
	if (!menu || menu != modmenuFind(loader) || tab <= 0)
		return;
	int enabled = canManageBots();
	if (enabled) {
		GameSettings* settings = gameGetSettings();
		Player** players = playerGetAll();
		for (i = 1; i < GAME_MAX_PLAYERS; ++i)
			if (!players[i] && (!BotsOnline || settings->PlayerClients[i] < 0))
				vacant = 1;
		for (i = 0; i < SimPlayerCount; ++i)
			if (SimPlayers[i].Created && SimPlayers[i].Player)
				mask |= 1 << i;
	}
	menu->setItemEnabled(spawnItem, enabled && vacant);
	for (i = 0; i < TRAINING_MAX_BOTS; ++i)
		menu->setItemEnabled(botItems[i], enabled);
	if (mask == listMask)
		return;
	listMask = mask;
	for (i = 0; i < TRAINING_MAX_BOTS; ++i) {
		modMenuButton_t kick = {"Kick", kickBot, (void*)(i + 1)};
		menu->setItemButtons(botItems[i], &kick, (mask & (1 << i)) ? 1 : 0);
	}
}

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
	pendingSpawn = pendingRemove = 0;
	listMask = -1;
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
	static const modMenuItem_t spawn = {"Spawn Bot", MODMENU_ACTION, 0, 0, 0, 0, spawnBot, 0};
	static const modMenuItem_t list = {"Bot List", MODMENU_HEADER, 0, 0, 0, 0, 0, 0};
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
	spawnItem = menu->addItem(tab, &spawn);
	menu->addItem(tab, &list);
	int i;
	for (i = 0; i < TRAINING_MAX_BOTS; ++i) {
		char label[16];
		sprintf(label, "Fake %d", i);
		modMenuItem_t row = {label, MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0};
		botItems[i] = menu->addItem(tab, &row);
	}
	listMask = -1;
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
	refreshBotList();
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
		lastMap = map;
		loadTime = settings->GameLoadStartTime;
		running = 1;
	}
	State.AggroMode = behavior == 0   ? TRAINING_AGGRESSION_IDLE
	                  : behavior == 1 ? TRAINING_AGGRESSION_PASSIVE
	                                  : TRAINING_AGGRESSION_AGGRO;
	time = gameGetTime();
	if ((time == lastTime || !SimPlayerCount) && !pendingSpawn && !pendingRemove)
		return;
	lastTime = time;
	uyaPreUpdate();
	startBots();
	if (pendingRemove) {
		if (botsRemove(pendingRemove - 1) && count > 0)
			--count;
		pendingRemove = 0;
	}
	if (pendingSpawn) {
		int result = botsSpawn();
		if (result >= 0)
			pendingSpawn = 0;
		if (result > 0 && count < TRAINING_MAX_BOTS)
			++count;
	}
	refreshBotList();
	uyaPostUpdate();
}
