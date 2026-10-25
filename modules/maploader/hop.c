/* Local Play development transitions. The retail request only schedules the
 * change; the game's main loop performs teardown and loading after we return. */
#include "maploader.h"
#include "modules/forge/include/forge.h"

#if UYA_MODULE_REGION == 2
#define LEVEL_ID          (0x1f83a8)
#define GAME_ACTIVE       (0x241a50)
#define SCENE_READY       (0x245848)
#define SETTINGS          (0x241640)
#define NET_PLAYERS       (0x22f3d0)
#define PLAYER_COUNT      (0x241644)
#define STAGING_MENU      (0x1c5c0c0)
#define STAGING_VTABLE    (0x47dda0)
#define START_GAME        (0x6af070)
#define START_GAME_HASH   (0x931aef7f)
#define HUD_LOAD_SETTINGS (0x8c631120)
#else
#define LEVEL_ID          (0x1f8528)
#define GAME_ACTIVE       (0x241bd0)
#define SCENE_READY       (0x2459c8)
#define SETTINGS          (0x2417c0)
#define NET_PLAYERS       (0x22f550)
#define PLAYER_COUNT      (0x2417c4)
#define STAGING_MENU      (0x1c5c0c0)
#define STAGING_VTABLE    (0x47de60)
#define START_GAME        (0x6ac720)
#define START_GAME_HASH   (0xeca152d0)
#define HUD_LOAD_SETTINGS (0x8c6312a0)
#endif

#define WORD(a) (*(volatile u32*)(a))
#ifndef MAP_HOP_CALL
#define MAP_HOP_CALL(address, base) ((void (*)(int))(address))(base)
#endif
#ifndef MAP_HOP_HUD_CALL
#define MAP_HOP_HUD_CALL(address) ((void (*)(void))(address))()
#endif
/* Local staging calls the regional START_GAME callback for its
 * Start Game action. Reuse retail roster/settings initialization unchanged.
 * Hash the whole callback before calling code from the staging overlay. */
#ifndef MAP_START_GAME_CALL
#define MAP_START_GAME_CALL(menu) ((void (*)(u32))START_GAME)(menu)
#endif

static u32 targetBase;
static int queued = -1, phase, ticks;
int mapHopSawLoading;
int mapHopResult;

/* The retail HUD loader only registers gameplay layers when the previous level
 * was staging (39). Reloads can clear the registrations even on the same base.
 * Run retail setup once after every completed Forge transition; its individual
 * registration routines guard their own initialization. */
static int restoreHud(u32 level)
{
#if UYA_MODULE_REGION == 2
	static const u32 addresses[] = {
	    0x4a8f48, 0x4ab060, 0x4a0938, 0x49e0d0, 0x49d420, 0x49acb8, 0x49acb0, 0x49d530, 0x49c830, 0x49c1b0};
#else
	static const u32 addresses[] = {
	    0x4a6a30, 0x4a8a88, 0x49e3a0, 0x49bbb8, 0x49af08, 0x498720, 0x4988d8, 0x49b118, 0x49a458, 0x499d98};
#endif
	static const u32 signature[] = {0x27bdffe0,
	                                         0x3c030024,
	                                         HUD_LOAD_SETTINGS,
	                                         0xffb00000,
	                                         0x0000202d,
	                                         0xffb10008,
	                                         0x24100004,
	                                         0xffbf0010,
	                                         0x0000882d,
	                                         0x8c620000,
	                                         0x28420002};
	u32 a, i;
	if (level < 40 || level > 49)
		return 0;
	a = addresses[level - 40];
	for (i = 0; i < sizeof(signature) / sizeof(signature[0]); ++i)
		if (WORD(a + i * 4) != signature[i])
			return 0;
	MAP_HOP_HUD_CALL(a);
	return 1;
}

static int prepareGameMode(void)
{
	const forgeApi_t* forge = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	return forge && forge->version == FORGE_API_VERSION && forge->size >= sizeof(*forge) &&
	       forge->active() && (!forge->applyGameMode || forge->applyGameMode());
}

int mapForgeActive(void)
{
	const forgeApi_t* p = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	return p && p->version == FORGE_API_VERSION && p->size >= sizeof(*p) && p->active();
}

int mapQuickLoadEnabled(void)
{
	const forgeApi_t* forge = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	return mapApi->context == UYA_CONTEXT_LOCAL && mapSelection >= 0 && mapSelection < mapCount &&
	       forge && forge->version == FORGE_API_VERSION && forge->size >= sizeof(*forge) &&
	       forge->active() && forge->quickLoadMaps && *forge->quickLoadMaps;
}

int mapHopInGame(void)
{
	u32 level = WORD(LEVEL_ID);
	return mapApi->context == UYA_CONTEXT_LOCAL && mapMultiplayerReady() && level >= 40 && level <= 49 &&
	       WORD(GAME_ACTIVE) == 1 && WORD(SCENE_READY) == 1;
}

static u32 findRequest(void)
{
	u32 a = mapSceneRequest;
	return a && WORD(a) == 0x27bdffe0 && WORD(a + 4) == 0xafa40000 && WORD(a + 8) == 0xffbf0010 &&
	               WORD(a + 40) == 0xaf82a934
	           ? a
	           : 0;
}

int mapHopAvailable(void)
{
	return mapCatalogReady && mapForgeActive() && mapHopInGame() && !phase && !mapLoading() && !mapScanActive() &&
	       mapHooksReady() && findRequest() != 0;
}

int mapHopRequest(int index)
{
	if (!mapHopAvailable() || (index < 0 ? (index > -40 || index < -49) : index >= mapCount))
		return 0;
	if (index >= 0 && !mapValidate(&mapRecords[index])) {
		mapMessage("Files changed; refresh first");
		return 0;
	}
	targetBase = index < 0 ? -index : mapRecords[index].base;
	mapHopResult = 1;
	queued = index;
	phase = 1;
	ticks = mapHopSawLoading = 0;
	mapMessage("Map hop queued");
	return 1;
}

static u32 startGameMenu(void)
{
	u32 menu, hash = 2166136261U, i;
	if (mapApi->context != UYA_CONTEXT_LOCAL || mapStaging() != 1)
		return 0;
	menu = WORD(STAGING_MENU);
	if (menu < 0x100000 || menu > 0x1fffc00 || (menu & 3) || WORD(menu + 0x60) != STAGING_VTABLE)
		return 0;
	for (i = 0; i < 240; ++i)
		hash = (hash ^ *(volatile u8*)(START_GAME + i)) * 16777619U;
	return hash == START_GAME_HASH ? menu : 0;
}

int mapStartGame(void)
{
	if (!mapForgeActive() || phase || mapLoading() || mapScanActive() || !mapHooksReady() || !startGameMenu())
		return 0;
	if (mapSelection >= 0 && (mapSelection >= mapCount || !mapValidate(&mapRecords[mapSelection])))
		return 0;
	mapHopResult = 1;
	phase = 3;
	ticks = 0;
	mapMessage("Start Game queued");
	return 1;
}

/* The ordinary transition preserves local network-player records. Re-entering
 * local player creation with pNetPlayerData still set makes 0x19a2c0 return
 * without attaching a player, and its caller dereferences NULL. Reset the same
 * records retail cleanup clears, after teardown reaches the new level TOC.
 * The game recreates these records from the existing match settings. */
void mapHopOnLoad(void)
{
	u32 i, j;
	if (phase != 2 || mapHopSawLoading || mapApi->context != UYA_CONTEXT_LOCAL)
		return;
	for (i = 0; i < 8; ++i) {
		volatile u32* p = (volatile u32*)(NET_PLAYERS + i * 0xc70);
		for (j = 0; j < 0xc70 / 4; ++j)
			p[j] = 0;
		p[0] = 0xffffffff;
	}
	WORD(PLAYER_COUNT) = 0;
	mapHopSawLoading = 1;
}

void mapHopTick(void)
{
	u32 settings, base, requestAddress;
	if (!phase)
		return;
	if (phase == 3) {
		u32 menu = startGameMenu();
		if (!menu || !mapForgeActive() || mapLoading() || !prepareGameMode()) {
			mapHopCancel();
			return;
		}
		phase = 4;
		mapMessage("Starting game...");
		mapQuickLoadBegin();
		MAP_START_GAME_CALL(menu);
		return;
	}
	if (phase == 4 && WORD(LEVEL_ID) >= 40 && WORD(LEVEL_ID) <= 55 && WORD(GAME_ACTIVE) == 1 &&
	    WORD(SCENE_READY) == 1 && !mapLoading()) {
		phase = 0;
		mapHopResult = 2;
		mapMessage("Game started");
		return;
	}
	if (phase == 1) {
		if (!mapForgeActive() || !mapHopInGame() || mapLoading()) {
			mapHopCancel();
			return;
		}
		settings = WORD(SETTINGS);
		if (settings < 0x100000 || settings > 0x1fffe00 || (settings & 3) || !findRequest()) {
			mapHopCancel();
			return;
		}
		requestAddress = findRequest();
		if (!prepareGameMode()) {
			mapHopCancel();
			return;
		}
		base = targetBase;
		mapSelection = queued < 0 ? -1 : queued;
		mapClearLoad();
		WORD(settings + 0x17c) = base;
		phase = 2;
		mapMessage("Loading map...");
		mapQuickLoadBegin();
		MAP_HOP_CALL(requestAddress, base);
		return;
	}
	++ticks;
	/* mapHopOnLoad marks the one-time reset at the level-loading boundary. */
	if (phase == 2 && mapHopSawLoading && mapHopInGame() && !mapLoading() && WORD(LEVEL_ID) == targetBase) {
		int hudReady = restoreHud(WORD(LEVEL_ID));
		mapHopResult = hudReady ? 2 : 3;
		phase = 0;
		queued = -1;
		mapMessage(hudReady ? "Map hop complete" : "HUD setup unavailable; return to staging");
	} else if (ticks > 12000) {
		mapHopResult = 3;
		phase = 0;
		queued = -1;
		mapMessage("Load timeout; restart game");
	}
}

void mapHopCancel(void)
{
	if (phase)
		mapHopResult = 3;
	phase = 0;
	queued = -1;
}
