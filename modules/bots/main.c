#include "module.h"
#include "modules/modmenu/include/modmenu.h"
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/uya.h>
#include <libuya/stdio.h>
#include "training/training.h"
#include "online.h"

#define HORIZON_ARENA_START (0x000c8000)

void moduleUpdate(void);

extern int SimPlayerCount;
extern int Initialized;
extern simulatedPlayer_t SimPlayers[];

static void refreshBotList(void);

static const moduleApi_t* loader;
static const modMenuApi_t* menu;
static int tab;
/* The saved default applies only when starting a Local Play game. */
static int defaultCount;
static int behavior;
static int configLoaded;
static u32 menuContext;
static const moduleConfigField_t configFields[] = {{"defaultBotsSpawned", &defaultCount, 0, TRAINING_MAX_BOTS}};
static int lastTime = -1;
static int lastMap = -1;
static int loadTime = -1;
static int running;
static int spawnItem;
#ifdef ONLINE_BOTS
static int networkItem;
#endif
static int pendingSpawn, pendingRemove;
static int listMask = -1;
static int botItems[TRAINING_MAX_BOTS];

static int canManageBots(void)
{
	return running && isInGame() && gameGetSettings() && (!BotsOnline || gameAmIHost());
}

static int hasVacantPlayerSlot(void)
{
	GameSettings* settings = gameGetSettings();
	Player** players = playerGetAll();
	int i;
	if (!settings || !players)
		return 0;
	/* Check the complete player roster, including humans and online slots
	 * reserved for clients whose actors haven't appeared yet. */
	for (i = 1; i < GAME_MAX_PLAYERS; ++i)
		if (!players[i] && (!BotsOnline || settings->PlayerClients[i] < 0))
			return 1;
	return 0;
}

static void spawnBot(void* context)
{
	(void)context;
	if (canManageBots() && hasVacantPlayerSlot())
		pendingSpawn = 1;
}

static void kickBot(void* context)
{
	if (canManageBots())
		pendingRemove = (int)context;
}

static void configAction(void* saving)
{
	if (loader->context != UYA_CONTEXT_LOCAL)
		return;
	int result = saving ? loader->saveConfig("bots", configFields, 1) : loader->loadConfig("bots", configFields, 1);
	loader->print("bots: %s settings result=%d\n", saving ? "save" : "load", result);
}

static void refreshBotList(void)
{
	int i, mask = 0;
	if (!menu || menu != modmenuFind(loader) || tab <= 0)
		return;
	int enabled = canManageBots();
	if (enabled) {
		for (i = 0; i < SimPlayerCount; ++i)
			if (SimPlayers[i].Created && SimPlayers[i].Player)
				mask |= 1 << i;
	}
	menu->setItemEnabled(spawnItem, enabled && hasVacantPlayerSlot());
#ifdef ONLINE_BOTS
	if (networkItem)
		menu->setItemEnabled(networkItem, enabled && !mask);
#endif
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
	static const modMenuItem_t amount = {
	    "Default Bots Spawned", MODMENU_RANGE, &defaultCount, 0, TRAINING_MAX_BOTS, 1, 0, 0};
	static const modMenuItem_t style = {"Behavior", MODMENU_RANGE, &behavior, 0, 2, 1, 0, 0};
	static const modMenuItem_t help = {
	    "Applies to the next Local Play game or reload", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	static const modMenuItem_t save = {"Save Settings", MODMENU_ACTION, 0, 0, 0, 0, configAction, (void*)1};
	static const modMenuItem_t load = {"Load Settings", MODMENU_ACTION, 0, 0, 0, 0, configAction, 0};
	static const modMenuItem_t spawn = {"Spawn Bot", MODMENU_ACTION, 0, 0, 0, 0, spawnBot, 0};
	static const modMenuItem_t list = {"Bot List", MODMENU_HEADER, 0, 0, 0, 0, 0, 0};
#ifdef ONLINE_BOTS
	static const modMenuItem_t network = {"Network Bot Prototype", MODMENU_TOGGLE, &BotsNetwork, 0, 1, 1, 0, 0};
	static const modMenuItem_t networkHelp = {
#if UYA_PAL
	    "PAL online support is untested",
#else
	    "Uses vacant player slots",
#endif
	    MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
#endif
	const modMenuApi_t* current = modmenuFind(loader);
	if (!current)
		return;
	if (current != menu) {
		menu = current;
		tab = 0;
	}
	/* Rebuild only this tab when crossing the online boundary. Other mods'
	 * controls and their configuration actions remain untouched. */
	if (tab > 0 && menuContext != loader->context) {
		menu->removeTab(tab);
		tab = 0;
	}
	if (!menu || tab > 0)
		return;
	tab = menu->addTab("bots", "Bots", UYA_MODE_MULTIPLAYER);
	if (tab <= 0)
		return;
	menu->setTabContexts(tab, UYA_CONTEXT_LOCAL | UYA_CONTEXT_ONLINE);
	menuContext = loader->context;
#ifdef ONLINE_BOTS
	networkItem = 0;
#endif
	if (menuContext == UYA_CONTEXT_LOCAL) {
		menu->addItem(tab, &amount);
		menu->addItem(tab, &help);
		menu->addItem(tab, &save);
		menu->addItem(tab, &load);
	}
#ifdef ONLINE_BOTS
	if (menuContext == UYA_CONTEXT_ONLINE) {
		networkItem = menu->addItem(tab, &network);
		menu->addItem(tab, &networkHelp);
	}
#endif
	menu->setItemValueLabel(menu->addItem(tab, &style), behaviorLabel);
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
	if (loader->context == UYA_CONTEXT_LOCAL && !configLoaded) {
		loader->loadConfig("bots", configFields, 1);
		configLoaded = 1;
	}
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
		/* Scene flags can switch before native hero construction finishes.
		 * Wait for its body, pad and net record before installing bot hooks. */
		Player** players = playerGetAll();
		Player* human = players ? players[0] : 0;
		if (!human || !human->pMoby || !human->pPad || !human->pNetPlayer)
			return;
		/* Online games always begin without automatic bots, regardless of
		 * saved Local Play defaults or manual changes in a previous game. */
		SimPlayerCount = BotsOnline ? 0 : defaultCount;
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
		botsRemove(pendingRemove - 1);
		pendingRemove = 0;
	}
	if (pendingSpawn) {
		int result = botsSpawn();
		if (result >= 0)
			pendingSpawn = 0;
	}
	refreshBotList();
	uyaPostUpdate();
}
