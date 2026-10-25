/* Development-only Local Play controls. Map Loader owns all transition work. */
#include "modules/forge/include/forge.h"
#include "modules/modmenu/include/modmenu.h"

#if UYA_MODULE_REGION == 2
#define BOOT_SITE (0x381568)
#define BOOT_ORIGINAL (0x0c0e8fdc)
#define BOOT_MULTIPLAYER (0x398128)
#define BOOT_EXEC (0x3980a0)
#define MULTIPLAYER_ARGS (0x1ebe00)
#define LEVEL_ID (0x1f83a8)
#define SCENE_READY (0x245848)
#define LANDING_UPDATE (0x6b0c90)
#define CREATE_GAME (0x6ac860)
#define UI_MANAGER (0x247914)
#define NET_INFO (0x1a5cc0)
#else
#define BOOT_SITE (0x381590)
#define BOOT_ORIGINAL (0x0c0e8f8c)
#define BOOT_MULTIPLAYER (0x398040)
#define BOOT_EXEC (0x397fb8)
#define MULTIPLAYER_ARGS (0x1ebf80)
#define LEVEL_ID (0x1f8528)
#define SCENE_READY (0x2459c8)
#define LANDING_UPDATE (0x6ae328)
#define CREATE_GAME (0x6a9f10)
#define UI_MANAGER (0x247a74)
#define NET_INFO (0x1a5e40)
#endif

#define WORD(address) (*(volatile u32*)(address))
#define BOOT_JUMP (0x08000000 | (BOOT_MULTIPLAYER >> 2))
#define CAMPAIGN_LEVEL (0x1d545c)
#define UI_POINTERS (0x1c5c064)
#define UI_ACTIVE (0x1c5c110)
#define BOOT_EXEC_CALL (BOOT_MULTIPLAYER + 0x4c)
#define BOOT_PARSED_OPTIONS (0x1d551d)

typedef enum forgeBootStage {
	FORGE_BOOT_INITIAL,
	FORGE_BOOT_MULTIPLAYER,
	FORGE_BOOT_CREATE,
	FORGE_BOOT_STAGING,
	FORGE_BOOT_DONE
} forgeBootStage_e;

/* Sony's EE startup argument block; pointer offsets describe the 32-bit ABI. */
typedef struct bootArguments {
	/* 0x00 */ u32 count;
	/* 0x04 */ const char* values[16];
	/* 0x44 */ char buffer[256];
} bootArguments_t;

void forgeBridgeInit(void);
void forgeBridgeDisable(void);
void forgeBridgeTick(const mapHopApi_t*);
void forgeBridgeResume(u32 requestId);
static int active(void);

static const moduleApi_t* loader;
static const modMenuApi_t* menu;
static int enabled, tab, item, refreshItem, lastReady = -1, lastRefresh = -1;
static int bootToLocalPlay = 1, bootRequested;
static forgeBootStage_e bootStage;
static forgeState_e bootResult;
static u32 bootRequestId;
static const moduleConfigField_t settings[] = {{"bootToLocalPlay", &bootToLocalPlay, 0, 1}};
static const forgeApi_t service = {FORGE_API_VERSION, sizeof(forgeApi_t), active};

static int active(void)
{
	return enabled && loader->context == UYA_CONTEXT_LOCAL;
}

static int execMultiplayer(const char* path, int count, char** values)
{
	char tag[15] = "FORGE=";
	char* arguments[3] = {values[0], values[1], tag};
	const char* hex = "0123456789abcdef";
	int i;
	(void)count;
	for (i = 0; i < 8; ++i)
		tag[6 + i] = hex[(bootRequestId >> (28 - i * 4)) & 15];
	tag[14] = 0;
	/* Preserve the two native arguments, including the untouched BOPT data.
	 * I5BOOT forwards our extra argument to the multiplayer ELF's _args. */
	return ((int (*)(const char*, int, char**))BOOT_EXEC)(path, 3, arguments);
}

static int resumeBoot(void)
{
	const volatile bootArguments_t* arguments = (void*)MULTIPLAYER_ARGS;
	const char* prefix = "FORGE=";
	u32 i, j, id;
	if (loader->mode != UYA_MODE_MULTIPLAYER || arguments->count > 16)
		return 0;
	for (i = 0; i < arguments->count; ++i) {
		const char* value = arguments->values[i];
		if ((u32)value < MULTIPLAYER_ARGS + 0x44 ||
		    (u32)value > MULTIPLAYER_ARGS + sizeof(*arguments) - 15)
			continue;
		for (j = 0; j < 6 && value[j] == prefix[j]; ++j)
			;
		if (j != 6 || value[14])
			continue;
		id = 0;
		for (j = 6; j < 14; ++j) {
			u32 digit = value[j] >= '0' && value[j] <= '9' ? value[j] - '0' :
			            value[j] >= 'a' && value[j] <= 'f' ? value[j] - 'a' + 10 : 16;
			if (digit == 16)
				break;
			id = (id << 4) | digit;
		}
		if (j != 14)
			continue;
		bootRequested = 1;
		bootRequestId = id;
		bootStage = FORGE_BOOT_MULTIPLAYER;
		bootResult = FORGE_LOADING;
		if (id)
			forgeBridgeResume(id);
		return 1;
	}
	return 0;
}

static u32 activeMenu(int index)
{
	u32 expected = WORD(UI_POINTERS + index * 4);
	int i;
	if (!expected)
		return 0;
	for (i = 2; i >= 0; --i) {
		u32 current = WORD(UI_ACTIVE + i * 4);
		if (current)
			return current == expected ? current : 0;
	}
	return 0;
}

int forgeBootAvailable(void)
{
	return enabled && loader->mode == UYA_MODE_CAMPAIGN && (int)WORD(CAMPAIGN_LEVEL) == -1 &&
	       WORD(BOOT_SITE) == BOOT_ORIGINAL && WORD(BOOT_MULTIPLAYER) == 0x3c02001d &&
	       WORD(BOOT_MULTIPLAYER + 4) == 0x27bdffc0 &&
	       WORD(BOOT_EXEC_CALL) == (0x0c000000 | (BOOT_EXEC >> 2)) &&
	       (bootStage == FORGE_BOOT_INITIAL || bootStage == FORGE_BOOT_DONE);
}

u32 forgeBootState(void)
{
	return bootResult;
}

static void bootLocalPlay(void)
{
	u32 current;
	if (bootStage == FORGE_BOOT_DONE)
		return;
	if (!bootToLocalPlay && !bootRequested) {
		bootStage = FORGE_BOOT_DONE;
		return;
	}
	if (loader->mode == UYA_MODE_CAMPAIGN) {
		/* Returning from multiplayer carries BOPT settings. Automatic boot
		 * only redirects a cold start; explicit requests can redirect again. */
		if (!bootRequested && *(volatile u8*)BOOT_PARSED_OPTIONS) {
			bootStage = FORGE_BOOT_DONE;
			return;
		}
		if (bootStage == FORGE_BOOT_INITIAL && forgeBootAvailable()) {
			/* Apply the supplied boot-to-multiplayer jump. Retail executes it
			 * after this tick returns, so ExecPS2 cannot strand the loader busy. */
			WORD(BOOT_EXEC_CALL) = 0x0c000000 | ((u32)execMultiplayer >> 2);
			WORD(BOOT_SITE) = BOOT_JUMP;
			loader->flushCache(0);
			loader->flushCache(2);
			bootStage = FORGE_BOOT_MULTIPLAYER;
		}
		return;
	}
	if (!active() || WORD(LEVEL_ID) != 39 || WORD(SCENE_READY) != 1)
		return;
	if (bootStage == FORGE_BOOT_INITIAL) {
		bootStage = FORGE_BOOT_DONE;
		return;
	}
	if (bootStage == FORGE_BOOT_MULTIPLAYER && (current = activeMenu(24))) {
		if (WORD(WORD(current + 0x60) + 0x14) != LANDING_UPDATE || WORD(LANDING_UPDATE) != 0x27bdffc0)
			return;
		/* Native UI select event on the Local Play entry. No controller data
		 * is injected; the landing screen initializes offline network state. */
		WORD(current + 0x290) = 1;
		((int (*)(u32, int))LANDING_UPDATE)(current, 7);
		bootStage = FORGE_BOOT_CREATE;
	} else if (bootStage == FORGE_BOOT_CREATE && (current = activeMenu(21))) {
		u32 manager = WORD(UI_MANAGER);
		if (!manager || WORD(CREATE_GAME) != 0x27bdfdf0) {
			bootStage = FORGE_BOOT_DONE;
			bootResult = FORGE_FAILED;
			return;
		}
		/* Same submission callback and next-screen assignment as retail's
		 * Create Game action. It builds the roster/settings, not a match. */
		if (((int (*)(u32))CREATE_GAME)(current)) {
			WORD(manager + 0x284) = 23;
			bootStage = FORGE_BOOT_STAGING;
		} else {
			bootStage = FORGE_BOOT_DONE;
			bootResult = FORGE_FAILED;
		}
	} else if (bootStage == FORGE_BOOT_STAGING && activeMenu(23)) {
		bootStage = FORGE_BOOT_DONE;
		bootResult = FORGE_COMPLETE;
	}
}

int forgeBootRequest(u32 requestId)
{
	const modMenuApi_t* currentMenu;
	if (!forgeBootAvailable())
		return FORGE_ERROR_CONTEXT;
	currentMenu = modmenuFind(loader);
	if (currentMenu)
		currentMenu->close();
	bootRequested = 1;
	bootRequestId = requestId;
	bootResult = FORGE_LOADING;
	bootStage = FORGE_BOOT_INITIAL;
	bootLocalPlay();
	return 0;
}

static void configAction(void* saving)
{
	int result = saving ? loader->saveConfig("forge", settings, 1) : loader->loadConfig("forge", settings, 1);
	loader->print("forge: %s settings result=%d\n", saving ? "save" : "load", result);
}

static const mapHopApi_t* hop(void)
{
	const mapHopApi_t* p = loader->findService(MAP_HOP_SERVICE_ID, FORGE_API_VERSION);
	return p && p->version == FORGE_API_VERSION && p->size >= sizeof(*p) ? p : 0;
}

static void reload(void* unused)
{
	const mapHopApi_t* p = hop();
	(void)unused;
	if (p && p->reload() > 0 && menu)
		menu->close();
}

static void refresh(void* unused)
{
	const mapHopApi_t* p = hop();
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
	if (bootResult == FORGE_LOADING && loader->mode == UYA_MODE_MULTIPLAYER &&
	    WORD(NET_INFO + 0x130) >= 5 && WORD(NET_INFO + 0x130) <= 12) {
		bootResult = FORGE_FAILED;
		bootStage = FORGE_BOOT_DONE;
	}
	forgeBridgeDisable();
	if (menu && tab > 0)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	lastReady = -1;
}

int moduleInit(const moduleApi_t* api)
{
	loader = api;
	forgeBridgeInit();
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	api->loadConfig("forge", settings, 1);
	resumeBoot();
	api->restrictContexts(UYA_CONTEXT_CAMPAIGN | UYA_CONTEXT_LOCAL);
	api->setLifecycle(activate, deactivate);
	enabled = 1;
	bootLocalPlay();
	return api->publishService(FORGE_SERVICE_ID, FORGE_API_VERSION, &service);
}

void moduleUpdate(void)
{
	bootLocalPlay();
	if (loader->mode != UYA_MODE_MULTIPLAYER) {
		forgeBridgeTick(0);
		return;
	}
	const mapHopApi_t* p = hop();
	int ready = p && p->available();
	static const modMenuItem_t action = {"Reload Current Map", MODMENU_ACTION, 0, 0, 0, 0, reload, 0};
	static const modMenuItem_t refreshAction = {"Refresh Custom Maps", MODMENU_ACTION, 0, 0, 0, 0, refresh, 0};
	static const modMenuItem_t boot = {"Boot to Local Play", MODMENU_TOGGLE, &bootToLocalPlay, 0, 1, 1, 0, 0};
	static const modMenuItem_t save = {"Save Settings", MODMENU_ACTION, 0, 0, 0, 0, configAction, (void*)1};
	static const modMenuItem_t load = {"Load Settings", MODMENU_ACTION, 0, 0, 0, 0, configAction, 0};
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
		menu->addItem(tab, &boot);
		menu->addItem(tab, &save);
		menu->addItem(tab, &load);
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
