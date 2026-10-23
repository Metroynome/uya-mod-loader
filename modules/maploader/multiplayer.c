/* Adapted from Horizon patch/maploader.c. This backend uses resident hostfs
 * reads; it does not install USB IRXs or depend on the patch's global state.
 * Every replaced call must still be the expected retail instruction. */
#include "maploader.h"
#include "hop_addresses.h"

#define WORD(a) (*(volatile u32*)(a))
#define JAL(a)  (0x0c000000 | ((u32)(a) >> 2))
#if UYA_MODULE_REGION == 2
#define IMAGE_SET       (0x6814b0)
#define MESSAGE         (0x59a970)
#define MESSAGE_CALL    (0x0c166a40)
#define MESSAGE_LOAD    (0x8c42bcc0)
#define CONNECTION      (0x1cd4cc)
#define NAME_HOOK       (0x6787a8)
#define NAME_TABLE      (0x352d20)
#define MAP_ID          (0x2412dc)
#define PART_ID         (0x240288)
#define LEVEL_ID        (0x1f83a8)
#define SCENE           (0x245848)
#define SETTINGS        (0x241640)
#define TOC_HOOK        (0x194f8c)
#define TOC_READ        (0x194ed8)
#define WAD_HOOK        (0x5a41c8)
#define WAD_READ        (0x5c2a40)
#define SYNC_HOOK       (0x5a4070)
#define CD_SYNC         (0x194a38)
#define CD_READ         (0x194970)
#define SOUND_HOOK      (0x5c1354)
#define SOUND_DISC      (0x193888)
#define SOUND_INIT_HOOK (0x5a41f4)
#define SOUND_INIT      (0x5f16f0)
#define SOUND_CORE_HOOK (0x5a41fc)
#define SOUND_CORE      (0x5c1310)
#define SOUND_BUSY      (0x240bc8)
#define SOUND_ERROR     (0x240b88)
#define SOUND_RESULT    (0x240140)
#define SOUND_PARAMS    (0x240180)
#define SOUND_RPC       (0x240100)
#define SOUND_FLUSH     (0x193590)
#else
#define IMAGE_SET       (0x67e990)
#define MESSAGE         (0x599618)
#define MESSAGE_CALL    (0x0c16656a)
#define MESSAGE_LOAD    (0x8c42bdc0)
#define CONNECTION      (0x1cd64c)
#define NAME_HOOK       (0x675dc0)
#define NAME_TABLE      (0x352e20)
#define MAP_ID          (0x24145c)
#define PART_ID         (0x240408)
#define LEVEL_ID        (0x1f8528)
#define SCENE           (0x2459c8)
#define SETTINGS        (0x2417c0)
#define TOC_HOOK        (0x19507c)
#define TOC_READ        (0x194fc8)
#define WAD_HOOK        (0x5a2560)
#define WAD_READ        (0x5c09f8)
#define SYNC_HOOK       (0x5a2408)
#define CD_SYNC         (0x194b28)
#define CD_READ         (0x194a60)
#define SOUND_HOOK      (0x5bf36c)
#define SOUND_DISC      (0x193978)
#define SOUND_INIT_HOOK (0x5a258c)
#define SOUND_INIT      (0x5eeff0)
#define SOUND_CORE_HOOK (0x5a2594)
#define SOUND_CORE      (0x5bf328)
#define SOUND_BUSY      (0x240d48)
#define SOUND_ERROR     (0x240d08)
#define SOUND_RESULT    (0x2402c0)
#define SOUND_PARAMS    (0x240300)
#define SOUND_RPC       (0x240280)
#define SOUND_FLUSH     (0x193680)
#endif
/* Resolve the retail name from GameLevel, never from a label that may already
 * contain a custom name (including after a live module reload). */
#ifndef MAP_MESSAGE
#define MAP_MESSAGE(id) ((const char* (*)(int))MESSAGE)(id)
#endif
/* Local and online staging both use child 1 for the map name. */
#ifndef MAP_IMAGE_SET
#define MAP_IMAGE_SET(child, texture) ((int (*)(u32, u32))IMAGE_SET)(child, texture)
#endif

typedef void (*SoundCallback)(int, unsigned long long);

typedef struct hook {
	u32 address, original;
	void* replacement;
} hook_t;

static void hookedTable(u32 sector, u32 count, void* dest);
static void hookedSound(int loc, int offset, SoundCallback callback, unsigned long long user);
static void hookedSoundInit(void);
static void hookedSoundCore(void);
static void hookedLoad(void* dest, u32 sector, u32 sectors);
static int hookedSync(int mode);

#if UYA_MODULE_REGION == 2
static const u32 radarHooks[10] = {
    0x4951a4, 0x4972bc, 0x48cb94, 0x48a264, 0x48967c, 0x486f14, 0x486f0c, 0x48978c, 0x488a8c, 0x48840c};
static const u32 localRadarHooks[6] = {0x4a61a4, 0x4a82bc, 0x49db94, 0x49b264, 0x49a67c, 0x497f14};
#else
static const u32 radarHooks[10] = {
    0x4931b4, 0x49520c, 0x48ab24, 0x488274, 0x48768c, 0x484ea4, 0x48505c, 0x48789c, 0x486bdc, 0x48651c};
static const u32 localRadarHooks[6] = {0x4a41b4, 0x4a620c, 0x49bb24, 0x499274, 0x49868c, 0x495ea4};
#endif
int mapAllowOnline;
u32 mapSceneRequest;
static u32 wadReader = WAD_READ, soundInit = SOUND_INIT, soundCore = SOUND_CORE;
static u32 nameHook = NAME_HOOK, nameTable = NAME_TABLE;
static mapRecord_t active;
static int armed, busy, failed, soundSent;
static void* soundBuffer;
static u32 savedSettings, savedBase, forcedBase;
static hook_t hooks[] = {{TOC_HOOK, JAL(TOC_READ), hookedTable},
                              {WAD_HOOK, JAL(WAD_READ), hookedLoad},
                              {SYNC_HOOK, JAL(CD_SYNC), hookedSync},
                              {SOUND_HOOK, JAL(SOUND_DISC), hookedSound},
                              {SOUND_INIT_HOOK, JAL(SOUND_INIT), hookedSoundInit},
                              {SOUND_CORE_HOOK, JAL(SOUND_CORE), hookedSoundCore}};

int mapMultiplayerReady(void)
{
	return mapApi->mode == UYA_MODE_MULTIPLAYER && WORD(0x12ae40) == 0x27bdff40 && WORD(0x12b080) == 0x27bdff20;
}

/* The lobby connection becomes non-null before online staging. */
int mapOnlineDisabled(void)
{
	return !mapAllowOnline && mapMultiplayerReady() && WORD(CONNECTION) != 0;
}

static int hookedName(void)
{
	u32 level = WORD(MAP_ID), i;
	if (!mapOnlineDisabled() && level >= 40 && level <= 59) {
		u32 base = 40 + (level - 40) % 10;
		const char* name = mapRetailName(base);
		char* dest = (char*)(nameTable + (base - 40) * 32);
		if (mapSelection >= 0 && mapSelection < mapCount && base == mapRecords[mapSelection].base)
			name = mapRecords[mapSelection].name;
		for (i = 0; i < 31 && name[i]; ++i)
			dest[i] = name[i];
		for (; i < 32; ++i)
			dest[i] = 0;
	}
	return 1;
}

int mapStaging(void)
{
	u32 i, local, online;
	if (!mapMultiplayerReady() || WORD(LEVEL_ID) != 39 || WORD(SCENE) != 1)
		return 0;
	local = WORD(0x1c5c064 + 23 * 4);
	online = WORD(0x1c5c064 + 33 * 4);
	for (i = 0; i < 3; ++i) {
		u32 menu = WORD(0x1c5c110 + i * 4);
		if (menu && menu == local)
			return 1;
		if (menu && menu == online)
			return 2;
	}
	return 0;
}

/* Names are also needed from gameplay overlays where the staging message
 * lookup no longer exists. Keep stock names separate from the patched table. */
const char* mapRetailName(u32 base)
{
	static const char* names[] = {"Bakisi Isles",
	                              "Hoven Gorge",
	                              "Outpost X12",
	                              "Korgon Outpost",
	                              "Metropolis",
	                              "Blackwater City",
	                              "Command Center",
	                              "Blackwater Docks",
	                              "Aquatos Sewers",
	                              "Marcadia Palace"};
	return base >= 40 && base <= 49 ? names[base - 40] : "Retail map";
}

u32 mapCurrentBase(void)
{
	u32 settings = WORD(SETTINGS);
	if (settings < 0x100000 || settings > 0x1fffe00 || (settings & 3))
		return 0;
	return WORD(settings + 0x17c);
}

static const char* retailMapName(void)
{
	return mapRetailName(mapCurrentBase());
}

static void writeStagingName(const char* name)
{
	u32 menu, child, i, base;
	int context = mapStaging();
	if (!name || !context)
		return;
	menu = WORD(0x1c5c064 + (context == 1 ? 23 : 33) * 4);
	if (menu < 0x100000 || menu > 0x1fffd00 || (menu & 3))
		return;
	if (WORD(menu + 0x10c) < 2 || WORD(menu + 0x10c) > 96)
		return;
	child = WORD(menu + 0x114);
	if (child < 0x100000 || child > 0x1ffff80 || (child & 3))
		return;
	for (i = 0; i < 31 && name[i]; ++i)
		((volatile char*)child)[0x14 + i] = name[i];
	for (; i < 32; ++i)
		((volatile char*)child)[0x14 + i] = 0;
	/* Retail local staging uses child 4 and texture 37 + (base - 40).
     * Call its setter: changing the ID alone does not upload the new texture. */
	if (context != 1 || WORD(menu + 0x10c) < 5)
		return;
	child = WORD(menu + 0x120);
	base = mapCurrentBase();
	if (child < 0x100000 || child > 0x1ffff80 || (child & 3) || base < 40 || base > 49)
		return;
	if (WORD(IMAGE_SET) == 0x27bdfff0 && WORD(IMAGE_SET + 16) == 0x8e02007c)
		MAP_IMAGE_SET(child, base - 3);
}

void mapUpdateName(void)
{
	if (!mapStaging() || mapOnlineDisabled())
		return;
	writeStagingName(mapSelection >= 0 && mapSelection < mapCount ? mapRecords[mapSelection].name : retailMapName());
}

int mapChooseBase(u32 base)
{
	u32 settings;
	/* Local staging has a different UI, but its callback reads this same
     * settings pointer and the halfword at +0x17c. Verified in the local
     * staging disassembly and an NTSC-U live lobby. The game chooses the
     * appropriate online/local overlay when starting the match.
     * Online settings require host/server synchronization, not a local write. */
	if (mapStaging() != 1 || base < 40 || base > 49)
		return 0;
	settings = WORD(SETTINGS);
	if (settings < 0x100000 || settings > 0x1fffe00 || (settings & 3) || (int)WORD(settings + 0x174) > 0 ||
	    WORD(settings + 0x17c) < 40 || WORD(settings + 0x17c) > 49)
		return 0;
	if (savedSettings != settings) {
		savedSettings = settings;
		savedBase = WORD(settings + 0x17c);
	}
	WORD(settings + 0x17c) = forcedBase = base;
	return 1;
}

int mapChooseRetail(u32 base)
{
	if (!mapChooseBase(base))
		return 0;
	/* An explicit retail choice becomes the baseline for subsequent overrides. */
	savedSettings = 0;
	mapSelection = -1;
	mapClearLoad();
	mapUpdateName();
	return 1;
}

void mapRestoreBase(void)
{
	if (mapStaging() == 1 && savedSettings && WORD(SETTINGS) == savedSettings &&
	    WORD(savedSettings + 0x17c) == forcedBase && (int)WORD(savedSettings + 0x174) <= 0)
		WORD(savedSettings + 0x17c) = savedBase;
	savedSettings = 0;
	/* Look up the name after restoring the ID, even if nothing was cached. */
	if (mapStaging())
		writeStagingName(retailMapName());
}

int mapLoading(void)
{
	return busy || failed;
}

void mapClearLoad(void)
{
	if (!busy) {
		armed = 0;
		failed = 0;
		soundSent = 0;
		soundBuffer = 0;
	}
}

static void loadError(void)
{
	failed = 1;
	mapMessage("Map read failed: fix files and restart");
	mapApi->print("maploader: load stopped; refusing to run partially replaced map data\n");
}

static int writable(void* dest, u32 size)
{
	u32 address = (u32)dest;
	/* Only the game's allocated EE data buffers, never module/kernel memory. */
	return address >= 0x100000 && address < 0x2000000 && size <= 0x2000000 - address;
}

static void hookedTable(u32 sector, u32 count, void* dest)
{
	u32 level = WORD(MAP_ID);
	((void (*)(u32, u32, void*))TOC_READ)(sector, count, dest);
	mapHopOnLoad();
	armed = failed = soundSent = 0;
	soundBuffer = 0;
	if (mapSelection < 0 || mapSelection >= mapCount)
		return;
	mapCopyRecord(&active, &mapRecords[mapSelection]);
	/* Local play loads 50..59, online play 40..49. Never reuse a selected
     * multiplayer WAD for campaign/menu or a different base map's code. */
	if (level != active.base && level != active.base + 10)
		return;
	busy = 1;
	if (!mapValidate(&active)) {
		busy = 0;
		mapMessage("Files changed; re-index before selecting");
		return;
	}
	busy = 0;
	armed = 1;
	if (active.wad)
		((u32*)dest)[5] = (active.wad + 0x7ff) / 0x800;
	if (active.world)
		((u32*)dest)[9] = (active.world + 0x7ff) / 0x800;
	mapApi->print("maploader: replacing map %u with %s\n", level, active.filename);
}

static int soundFromEE(void* buffer)
{
	int result, spins = 0;
	if (WORD(SOUND_BUSY))
		return -1;
	WORD(SOUND_ERROR) = 0;
	WORD(SOUND_RESULT) = 0xffffffff;
	WORD(SOUND_PARAMS) = (u32)buffer;
	((void (*)(void*, void*))0x126b28)((void*)SOUND_RESULT, (void*)(SOUND_RESULT + 4));
	while (((int (*)(void*))0x12a018)((void*)SOUND_RPC)) {
		if (++spins > 1000000)
			return -1;
		((void (*)(void))SOUND_FLUSH)();
		mapApi->flushCache(0);
	}
	WORD(SOUND_BUSY) = 1;
	result = ((int (*)(void*, int, int, void*, int, void*, int, void*, void*))0x129e18)(
	    (void*)SOUND_RPC, 0x57, 1, (void*)SOUND_PARAMS, 4, (void*)SOUND_RESULT, 4, 0, 0);
	if (result < 0) {
		WORD(SOUND_ERROR) = 0x106;
		return -1;
	}
	spins = 0;
	do {
		mapApi->flushCache(0);
		result = (int)WORD(SOUND_RESULT);
		if (++spins > 1000000)
			return -1;
	} while (result == -1);
	return result;
}

static void hookedSound(int loc, int offset, SoundCallback callback, unsigned long long user)
{
	if (armed && soundBuffer) {
		int result;
		if (soundSent)
			return;
		WORD(SOUND_BUSY + 8) = (u32)callback;
		*(volatile unsigned long long*)(SOUND_BUSY + 16) = user;
		result = soundFromEE(soundBuffer);
		if (result < 0) {
			loadError();
			return;
		}
		if (callback)
			callback(result, user);
		soundSent = 1;
		return;
	}
	((void (*)(int, int, SoundCallback, unsigned long long))SOUND_DISC)(loc, offset, callback, user);
}

static void hookedSoundInit(void)
{
	if (!armed || !soundSent)
		((void (*)(void))soundInit)();
}

static void hookedSoundCore(void)
{
	if (!armed || !soundSent)
		((void (*)(void))soundCore)();
}

static void hookedLoad(void* dest, u32 sector, u32 sectors)
{
	u32 part = WORD(PART_ID), size;
	const char* extension;
	if (!armed || part > 1) {
		((void (*)(void*, u32, u32))wadReader)(dest, sector, sectors);
		return;
	}
	size = part ? active.world : active.wad;
	extension = part ? ".world" : ".wad";
	busy = 1;
	if (part == 0 && active.sound && !soundSent) {
		/* Sound is staged in the level buffer and uploaded before the WAD
         * replaces it, matching Horizon's load ordering. */
		if (active.sound > sectors * 0x800 || !writable(dest, active.sound) ||
		    !mapReadPart(&active, ".sound", dest, active.sound)) {
			loadError();
			busy = 0;
			return;
		}
		soundBuffer = dest;
		mapApi->flushCache(0);
		((void (*)(void))soundInit)();
		((void (*)(void))soundCore)();
		soundBuffer = 0;
		if (!soundSent || failed) {
			loadError();
			busy = 0;
			return;
		}
	}
	if (size) {
		if (sectors > 0x4000 || size > sectors * 0x800 || !writable(dest, sectors * 0x800) ||
		    !mapReadPart(&active, extension, dest, size)) {
			loadError();
			busy = 0;
			return;
		}
		/* Disc reads are sector padded; file payloads need not be. */
		{
			u8* p = (u8*)dest + size;
			u32 remaining = sectors * 0x800 - size;
			while (remaining--)
				*p++ = 0;
		}
		mapApi->flushCache(0);
	} else {
		((void (*)(void*, u32, u32))wadReader)(dest, sector, sectors);
	}
	busy = 0;
}

static int hookedSync(int mode)
{
	int result = ((int (*)(int))CD_SYNC)(mode);
	return failed ? 1 : result;
}

static int hookedRadar(u32 sector, u32 count, void* dest, void* args)
{
	if (armed && active.radar && count == 0x21 && writable(dest, 0x10800)) {
		int ok;
		busy = 1;
		ok = mapReadPart(&active, ".map", dest, active.radar);
		busy = 0;
		if (ok) {
			mapApi->flushCache(0);
			return 0;
		}
	}
	return ((int (*)(u32, u32, void*, void*))CD_READ)(sector, count, dest, args);
}

/* Loading code is map-specific after leaving staging. Switch the entire hook
 * group only when its retail/current-owned calls match the requested overlay. */
static void configureHooks(void)
{
	u32 level = WORD(LEVEL_ID), i, a[6], o[6];
	const u32* row;
	if (level < 39 || level > 49)
		return;
	row = hopAddresses[level - 39];
	if (hooks[1].address == row[0]) {
		mapSceneRequest = row[4];
		return;
	}
	a[0] = TOC_HOOK;
	a[1] = row[0];
	a[2] = row[0] - 0x158;
	a[3] = row[3] + 0x44;
	a[4] = row[0] + 0x2c;
	a[5] = row[0] + 0x34;
	o[0] = JAL(TOC_READ);
	o[1] = JAL(row[1]);
	o[2] = JAL(CD_SYNC);
	o[3] = JAL(SOUND_DISC);
	o[4] = JAL(row[2]);
	o[5] = JAL(row[3]);
	for (i = 0; i < 6; ++i)
		if (WORD(a[i]) != o[i] && WORD(a[i]) != JAL(hooks[i].replacement))
			return;
	/* The level ID changes before the old overlay finishes drawing its loading
	 * screen. Keep its name table until the new overlay's code is present. */
	nameHook = row[5];
	nameTable = row[6];
	for (i = 0; i < 6; ++i) {
		hooks[i].address = a[i];
		hooks[i].original = o[i];
	}
	wadReader = row[1];
	soundInit = row[2];
	soundCore = row[3];
	mapSceneRequest = row[4];
}

static int radarMatches(u32 address)
{
	static const u32 signature[10] = {0x0260202d,
	                                           0xae000018,
	                                           0xae00001c,
	                                           0x0220282d,
	                                           0x0240302d,
	                                           JAL(CD_READ),
	                                           0x26070040,
	                                           0x10400007,
	                                           0x24020001,
	                                           0xae120014};
	u32 i;
	for (i = 0; i < 10; ++i)
		if (WORD(address - 20 + i * 4) != signature[i])
			return 0;
	return 1;
}

int mapHooksReady(void)
{
	u32 i;
	if (!mapMultiplayerReady() || mapOnlineDisabled())
		return 0;
	for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i)
		if (WORD(hooks[i].address) != JAL(hooks[i].replacement))
			return 0;
	return 1;
}

void mapDisableHooks(void)
{
	u32 i, changed = 0, level;
	if (!mapMultiplayerReady() || busy)
		return;
	/* Restore only instructions still owned by this module. A competing
     * patch must never be replaced with our saved retail instructions. */
	for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i)
		if (WORD(hooks[i].address) == JAL(hooks[i].replacement)) {
			WORD(hooks[i].address) = hooks[i].original;
			changed = 1;
		}
	if (WORD(nameHook) == (0x08000000U | ((u32)hookedName >> 2)) && WORD(nameHook + 4) == 0x27bd0050) {
		WORD(nameHook) = 0x03e00008;
		changed = 1;
	}
	level = WORD(LEVEL_ID);
	if (level >= 40 && level <= 55) {
		u32 address = level < 50 ? radarHooks[level - 40] : localRadarHooks[level - 50];
		if (WORD(address) == JAL(hookedRadar) && WORD(address + 4) == 0x26070040) {
			WORD(address) = JAL(CD_READ);
			changed = 1;
		}
	}
	mapSelection = -1;
	mapClearLoad();
	savedSettings = 0;
	if (changed) {
		mapApi->flushCache(0);
		mapApi->flushCache(2);
	}
}

/* Horizon leaves its map-loading calls installed when returning to Local Play.
 * Match the first 64 bytes of each callback, not just a jump into low memory.
 * These FNV-1a signatures come from patch-10683/10684; NTSC-U was also read
 * live after logout. Unknown patch builds remain untouched. */
static int horizonCall(u32 instruction, u32 opcode, u32 signature)
{
	u32 address = (instruction & 0x03ffffff) << 2;
	u32 hash = 2166136261, i;
	if ((instruction & 0xfc000000) != opcode || address < 0x000d0000 || address > 0x000effc0)
		return 0;
	for (i = 0; i < 64; ++i)
		hash = (hash ^ *(volatile u8*)(address + i)) * 16777619;
	return hash == signature;
}

static int reclaimLocalHooks(void)
{
#if UYA_MODULE_REGION == 2
	static const u32 signatures[] = {0x46b8a999, 0x7bee231d, 0x0a7d2770, 0xc4c726eb, 0x67355e77, 0x57a96645};
	const u32 exitHook = 0x193340, exitCall = JAL(0x192f68);
#else
	static const u32 signatures[] = {0xaedf49cb, 0x1e0cdf27, 0x0a7d2770, 0x5b912728, 0x99239e38, 0x314282cf};
	const u32 exitHook = 0x193430, exitCall = JAL(0x193058);
#endif
	const u32 addresses[] = {TOC_HOOK, WAD_HOOK, SYNC_HOOK, SOUND_HOOK};
	const u32 originals[] = {JAL(TOC_READ), JAL(WAD_READ), JAL(CD_SYNC), JAL(SOUND_DISC)};
	u32 i, name = WORD(NAME_HOOK), exit = WORD(exitHook);
	if (mapApi->context != UYA_CONTEXT_LOCAL || WORD(CONNECTION) || mapStaging() != 1)
		return 0;
	/* Require the complete known Horizon group before changing any call. */
	for (i = 0; i < 4; ++i)
		if (!horizonCall(WORD(addresses[i]), 0x0c000000, signatures[i]))
			return 0;
	if ((WORD(SOUND_INIT_HOOK) && WORD(SOUND_INIT_HOOK) != JAL(SOUND_INIT)) ||
	    (WORD(SOUND_CORE_HOOK) && WORD(SOUND_CORE_HOOK) != JAL(SOUND_CORE)))
		return 0;
	if (name != 0x03e00008 && name != (0x08000000 | ((u32)hookedName >> 2)) &&
	    !horizonCall(name, 0x08000000, signatures[4]))
		return 0;
	if (exit != exitCall && !horizonCall(exit, 0x0c000000, signatures[5]))
		return 0;
	for (i = 0; i < 4; ++i)
		WORD(addresses[i]) = originals[i];
	WORD(SOUND_INIT_HOOK) = JAL(SOUND_INIT);
	WORD(SOUND_CORE_HOOK) = JAL(SOUND_CORE);
	if (name != (0x08000000 | ((u32)hookedName >> 2)))
		WORD(NAME_HOOK) = 0x03e00008;
	WORD(exitHook) = exitCall;
	return 1;
}

void mapInstallHooks(void)
{
	u32 i, changed = 0, level;
	if (!mapMultiplayerReady() || busy)
		return;
	if (mapOnlineDisabled()) {
		mapDisableHooks();
		return;
	}
	changed |= reclaimLocalHooks();
	configureHooks();
	if (WORD(LEVEL_ID) != 39)
		savedSettings = 0;
	/* Tail-hook the transition name builder, preserving its stack delay slot. */
	if (WORD(nameHook) == 0x03e00008 && WORD(nameHook + 4) == 0x27bd0050 && WORD(nameHook - 4) == 0xdfbf0048 &&
	    WORD(nameHook - 8) == 0xdfbe0040) {
		WORD(nameHook) = 0x08000000U | ((u32)hookedName >> 2);
		changed = 1;
	}
	/* Install the loading group atomically: never combine our loader with a
     * Horizon/other mod's half-owned hook set. Overlay reloads restore retail
     * calls; accept either retail or our own instruction on reinstallation. */
	for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
		u32 instruction = WORD(hooks[i].address);
		if (instruction != hooks[i].original && instruction != JAL(hooks[i].replacement))
			break;
	}
	if (i == sizeof(hooks) / sizeof(hooks[0])) {
		for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
			u32 instruction = JAL(hooks[i].replacement);
			if (WORD(hooks[i].address) != instruction) {
				WORD(hooks[i].address) = instruction;
				changed = 1;
			}
		}
	}
	level = WORD(LEVEL_ID);
	/* Local radar hooks were separately signature-matched in code5 levels
     * 50..55. There are no split-screen counterparts for online bases 46..49. */
	if (armed && ((level >= 40 && level <= 49) || (level >= 50 && level <= 55))) {
		u32 address = level < 50 ? radarHooks[level - 40] : localRadarHooks[level - 50];
		if (radarMatches(address)) {
			WORD(address) = JAL(hookedRadar);
			changed = 1;
		}
	}
	if (changed) {
		mapApi->flushCache(0);
		mapApi->flushCache(2);
	}
}
