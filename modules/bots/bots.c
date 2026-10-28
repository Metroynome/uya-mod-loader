#include <tamtypes.h>
#include <string.h>

#include <libuya/time.h>
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/player.h>
#include <libuya/camera.h>
#include <libuya/weapon.h>
#include <libuya/sha1.h>
#include <libuya/ui.h>
#include <libuya/stdio.h>
#include <libuya/graphics.h>
#include <libuya/net.h>
#include <libuya/pad.h>
#include <libuya/uya.h>
#include <libuya/utils.h>
#include <libuya/interop.h>
#include <libuya/math3d.h>
#include "training/training.h"

#define BOT_PAD_LOCATION  ((u32)PAD_POINTER + 0xc)
#define BOT_LOCAL_STRIDE  (0x4500)
#define BOT_REMOTE_STRIDE (0x4dc0)
#define BOT_SCRATCH_SLOT  (3)
#if UYA_PAL
#define BOT_NET_DATA_COPY (0x0019a39c)
#else
#define BOT_NET_DATA_COPY (0x0019a4dc)
#endif

typedef struct botSlot {
	/* 0x00 */ int id;
	/* 0x04 */ short client;
	/* 0x06 */ short skin;
	/* 0x08 */ short team;
	/* 0x0a */ char name[16];
} botSlot_t;

/* EE pointer layout; host pointer widths may differ. */
typedef struct botNotifications {
	/* 0x00 */ volatile int* count;
	/* 0x04 */ volatile int* timers;
	/* 0x08 */ char* messages;
} botNotifications_t;

void modeProcessPlayer(int pIndex);
void modeTick(void);
void modeInitTarget(simulatedPlayer_t* sPlayer);
void modeInitialize(void);
void FlushCache(int operation);
void modeUpdateTarget(simulatedPlayer_t* sPlayer);
void frameTick(void);
void gameTick(void);
static Player* botGetFromSlot(int slot);

extern VariableAddress_t vaGetMissionDef;

VariableAddress_t vaInit_RunUpdateFunctions = {
#if UYA_PAL
	.Lobby = 0x0061ce78,
	.Bakisi = 0x004eed70,
	.Hoven = 0x004f0e88,
	.OutpostX12 = 0x004e6760,
	.KorgonOutpost = 0x004e3ef8,
	.Metropolis = 0x004e3248,
	.BlackwaterCity = 0x004e0ae0,
	.CommandCenter = 0x004e0aa8,
	.BlackwaterDocks = 0x004e3328,
	.AquatosSewers = 0x004e2628,
	.MarcadiaPalace = 0x004e1fa8,
#else
	.Lobby = 0x0061a700,
	.Bakisi = 0x004ec650,
	.Hoven = 0x004ee6a8,
	.OutpostX12 = 0x004e3fc0,
	.KorgonOutpost = 0x004e17d8,
	.Metropolis = 0x004e0b28,
	.BlackwaterCity = 0x004de340,
	.CommandCenter = 0x004de4c8,
	.BlackwaterDocks = 0x004e0d08,
	.AquatosSewers = 0x004e0048,
	.MarcadiaPalace = 0x004df988,
#endif
};

VariableAddress_t vaCreateHero = {
#if UYA_PAL
	.Lobby = 0x00661a40,
	.Bakisi = 0x00533b40,
	.Hoven = 0x00535c58,
	.OutpostX12 = 0x0052b530,
	.KorgonOutpost = 0x00528cc8,
	.Metropolis = 0x00528018,
	.BlackwaterCity = 0x005258b0,
	.CommandCenter = 0x00525670,
	.BlackwaterDocks = 0x00527ef0,
	.AquatosSewers = 0x005271f0,
	.MarcadiaPalace = 0x00526b70,
#else
	.Lobby = 0x0065f168,
	.Bakisi = 0x005312c0,
	.Hoven = 0x00533318,
	.OutpostX12 = 0x00528c30,
	.KorgonOutpost = 0x00526448,
	.Metropolis = 0x00525798,
	.BlackwaterCity = 0x00522fb0,
	.CommandCenter = 0x00522f30,
	.BlackwaterDocks = 0x00525770,
	.AquatosSewers = 0x00524ab0,
	.MarcadiaPalace = 0x005243f0,
#endif
};

VariableAddress_t vaPAD_PadUpdate = {
#if UYA_PAL
	.Lobby = 0x005c34a8,
	.Bakisi = 0x00495c60,
	.Hoven = 0x00497d78,
	.OutpostX12 = 0x0048d650,
	.KorgonOutpost = 0x0048ad20,
	.Metropolis = 0x0048a138,
	.BlackwaterCity = 0x004879d0,
	.CommandCenter = 0x004879c8,
	.BlackwaterDocks = 0x0048a248,
	.AquatosSewers = 0x00489548,
	.MarcadiaPalace = 0x00488ec8,
#else
	.Lobby = 0x005c1258,
	.Bakisi = 0x00493a68,
	.Hoven = 0x00495ac0,
	.OutpostX12 = 0x0048b3d8,
	.KorgonOutpost = 0x00488b28,
	.Metropolis = 0x00487f40,
	.BlackwaterCity = 0x00485758,
	.CommandCenter = 0x00485910,
	.BlackwaterDocks = 0x00488150,
	.AquatosSewers = 0x00487490,
	.MarcadiaPalace = 0x00486dd0,
#endif
};

static VariableAddress_t vaSetFpsCamera = {
#if UYA_PAL
	.Bakisi = 0x00518c98,
	.Hoven = 0x0051adb0,
	.OutpostX12 = 0x00510688,
	.KorgonOutpost = 0x0050de20,
	.Metropolis = 0x0050d170,
	.BlackwaterCity = 0x0050aa08,
	.CommandCenter = 0x0050a7c8,
	.BlackwaterDocks = 0x0050d048,
	.AquatosSewers = 0x0050c348,
	.MarcadiaPalace = 0x0050bcc8,
#else
	.Bakisi = 0x005164c0,
	.Hoven = 0x00518518,
	.OutpostX12 = 0x0050de30,
	.KorgonOutpost = 0x0050b648,
	.Metropolis = 0x0050a998,
	.BlackwaterCity = 0x005081b0,
	.CommandCenter = 0x00508130,
	.BlackwaterDocks = 0x0050a970,
	.AquatosSewers = 0x00509cb0,
	.MarcadiaPalace = 0x005095f0,
#endif
};

/* The native remote pool has eight objects, unlike the four local heroes.
 * Only vacant roster slots are used, retaining local training inputs. */
static VariableAddress_t vaCreateRemoteHero = {
#if UYA_PAL
	.Bakisi = 0x00538618,
	.Hoven = 0x0053a730,
	.OutpostX12 = 0x00530008,
	.KorgonOutpost = 0x0052d7a0,
	.Metropolis = 0x0052caf0,
	.BlackwaterCity = 0x0052a388,
	.CommandCenter = 0x0052a148,
	.BlackwaterDocks = 0x0052c9c8,
	.AquatosSewers = 0x0052bcc8,
	.MarcadiaPalace = 0x0052b648,
#else
	.Bakisi = 0x00535d98,
	.Hoven = 0x00537df0,
	.OutpostX12 = 0x0052d708,
	.KorgonOutpost = 0x0052af20,
	.Metropolis = 0x0052a270,
	.BlackwaterCity = 0x00527a88,
	.CommandCenter = 0x00527a08,
	.BlackwaterDocks = 0x0052a248,
	.AquatosSewers = 0x00529588,
	.MarcadiaPalace = 0x00528ec8,
#endif
};

static VariableAddress_t vaPlayerLeave = {
#if UYA_PAL
	.Bakisi = 0x00538f48,
	.Hoven = 0x0053b060,
	.OutpostX12 = 0x00530938,
	.KorgonOutpost = 0x0052e0d0,
	.Metropolis = 0x0052d420,
	.BlackwaterCity = 0x0052acb8,
	.CommandCenter = 0x0052aa78,
	.BlackwaterDocks = 0x0052d2f8,
	.AquatosSewers = 0x0052c5f8,
	.MarcadiaPalace = 0x0052bf78,
#else
	.Bakisi = 0x005366c8,
	.Hoven = 0x00538720,
	.OutpostX12 = 0x0052e038,
	.KorgonOutpost = 0x0052b850,
	.Metropolis = 0x0052aba0,
	.BlackwaterCity = 0x005283b8,
	.CommandCenter = 0x00528338,
	.BlackwaterDocks = 0x0052ab78,
	.AquatosSewers = 0x00529eb8,
	.MarcadiaPalace = 0x005297f8,
#endif
};

int Initialized = 0;
int BotsOnline;
trainingState_t State;
static int startDelay = 15;
static u32 updateHook;
static botSlot_t savedSlots[TRAINING_MAX_BOTS];
static GameSettings* savedSettings;
static int savedLoadTime;
static u32 getterHook;
static u32 getterOriginal[2];
static Player* localHeroes;
static Player* scopedPlayer;
static GameCamera extraCameras[TRAINING_MAX_BOTS - 3];
static u8 extraNetData[TRAINING_MAX_BOTS - 3][0x90] __attribute__((aligned(16)));

extern int TargetTeam;
extern int SimPlayerCount;
extern simulatedPlayer_t SimPlayers[];

const char* TRAINING_AGGRO_NAMES[] = {
    [TRAINING_AGGRESSION_AGGRO] "AGGRO",
    [TRAINING_AGGRESSION_AGGRO_NO_DAMAGE] "NO DAMAGE",
    [TRAINING_AGGRESSION_PASSIVE] "PASSIVE",
    [TRAINING_AGGRESSION_IDLE] "IDLE",
};

static void flushCode(void)
{
	FlushCache(0);
	FlushCache(2);
}

/* Native camera helpers ask for a local ordinal. During an extra bot's
 * constructor/update, route its scratch ordinal to that bot's own object. */
static Player* botGetFromSlot(int slot)
{
	if (scopedPlayer && slot == BOT_SCRATCH_SLOT)
		return scopedPlayer;
	return (Player*)((u32)localHeroes + slot * BOT_LOCAL_STRIDE);
}

static Player* createExtraPlayer(int id)
{
	u32* remote = (u32*)GetAddressImmediate(&vaCreateRemoteHero);
	u32* create = (u32*)GetAddressImmediate(&vaCreateHero);
	u32* getter = (u32*)GetAddressImmediate(&vaGetMissionDef);
	u32 original[3];
	u32 netOriginal[3];
	u32* net = (u32*)BOT_NET_DATA_COPY;
	u32 netAddress = (u32)extraNetData[id - 4] - id * 0x90;
	short clients[GAME_MAX_PLAYERS];
	GameSettings* settings = gameGetSettings();
	GameCamera* scratch = cameraGetGameCamera(BOT_SCRATCH_SLOT);
	GameCamera camera;
	Player* player;
	u32 address;
	int i;

	/* These instruction layouts were checked in every supported map overlay.
	 * Refuse an unfamiliar/modified constructor instead of touching its pool. */
	if (!remote || !create || !getter || !scratch || remote[0x3c / 4] != 0x24044dc0 || remote[0x44 / 4] != 0x8c740054 ||
	    (remote[0x40 / 4] & 0xffff0000) != 0x3c020000 || (remote[0x48 / 4] & 0xffff0000) != 0x24420000 ||
	    create[1] != 0x24024500 || (create[3] & 0xffff0000) != 0x3c030000 || (create[7] & 0xffff0000) != 0x24630000 ||
	    net[0] != 0x24030090 || (net[1] & 0xffff0000) != 0x3c020000 || (net[4] & 0xffff0000) != 0x24420000 ||
	    (net[16] & 0xffff0000) != 0x24e30000)
		return 0;
	address = (remote[0x40 / 4] << 16) + (s16)remote[0x48 / 4] + id * BOT_REMOTE_STRIDE;
	player = (Player*)address;
	if (!getterHook) {
		if (getter[0] != 0x24034500 || (getter[1] & 0xffff0000) != 0x3c020000)
			return 0;
		localHeroes = playerGetFromSlot(0);
		getterOriginal[0] = getter[0];
		getterOriginal[1] = getter[1];
		getterHook = (u32)getter;
		HOOK_J(getterHook, botGetFromSlot);
		getter[1] = 0;
	}

	/* Reuse a valid local controller/profile ordinal, never index their
	 * four-entry arrays with a network player ID (4-7). Restore the roster
	 * and camera before returning to the game's normal frame. */
	memcpy(clients, settings->PlayerClients, sizeof(clients));
	/* Remote humans may precede the vacant slot online. Present exactly
	 * three local ordinals to this constructor, then restore every client. */
	for (i = 0; i < id; ++i)
		settings->PlayerClients[i] = i < BOT_SCRATCH_SLOT ? GAME_CLIENT_ID : -1;
	memcpy(&camera, scratch, sizeof(camera));
	original[0] = create[1];
	original[1] = create[3];
	original[2] = create[7];
	create[1] = 0x24020000;
	create[3] = 0x3c030000 | ((address + 0x8000) >> 16);
	create[7] = 0x24630000 | (address & 0xffff);
	/* The Local Play net-data array also has only four entries. Redirect
	 * this copy for the constructor call; the eight-entry tNW_Player table
	 * keeps the resulting pointer after the original instructions return. */
	netOriginal[0] = net[1];
	netOriginal[1] = net[4];
	netOriginal[2] = net[16];
	net[1] = 0x3c020000 | ((netAddress + 0x8000) >> 16);
	net[4] = 0x24420000 | (netAddress & 0xffff);
	net[16] = 0x24e30000 | (netAddress & 0xffff);
	scopedPlayer = player;
	flushCode();
	((void (*)(int))create)(BOT_SCRATCH_SLOT);
	scopedPlayer = 0;
	create[1] = original[0];
	create[3] = original[1];
	create[7] = original[2];
	net[1] = netOriginal[0];
	net[4] = netOriginal[1];
	net[16] = netOriginal[2];
	flushCode();
	memcpy(settings->PlayerClients, clients, sizeof(clients));
	memcpy(&extraCameras[id - 4], scratch, sizeof(camera));
	memcpy(scratch, &camera, sizeof(camera));
	player->camera = &extraCameras[id - 4];
	return playerGetAll()[id] == player ? player : 0;
}

void createSimPlayer(simulatedPlayer_t* sPlayer, int idx)
{
	int id;
	/* Never replace a human, including split-screen and remote clients. */
	for (id = 1; id < GAME_MAX_PLAYERS; ++id) {
		if (!playerGetAll()[id] && (!BotsOnline || gameGetSettings()->PlayerClients[id] < 0))
			break;
	}
	if (id == GAME_MAX_PLAYERS)
		return;
	GameSettings* gameSettings = gameGetSettings();
	Player** players = playerGetAll();
	if (!players || !players[0] || !players[0]->pPad)
		return;

	// HOOK_JAL(0x00610e38, &fixSimPlayerNetPlayerPtr);

	// reset
	memset(sPlayer, 0, sizeof(simulatedPlayer_t));
	sPlayer->Idx = idx;

	// spawn player
	memcpy(&sPlayer->Pad, players[0]->pPad, sizeof(struct PAD));

	sPlayer->Pad.rdata[7] = 0x7f;
	sPlayer->Pad.rdata[6] = 0x7f;
	sPlayer->Pad.rdata[5] = 0x7f;
	sPlayer->Pad.rdata[4] = 0x7f;
	sPlayer->Pad.rdata[3] = 0xff;
	sPlayer->Pad.rdata[2] = 0xff;
	sPlayer->Pad.port = 10;
	sPlayer->Pad.slot = 10;
	sPlayer->Pad.id = id;

	// Preserve the vacant slot so it is available again after this game.
	botSlot_t* saved = &savedSlots[idx];
	saved->id = id;
	saved->client = gameSettings->PlayerClients[id];
	saved->skin = gameSettings->PlayerSkins[id];
	saved->team = gameSettings->PlayerTeams[id];
	memcpy(saved->name, gameSettings->PlayerNames[id], sizeof(saved->name));
	savedSettings = gameSettings;
	savedLoadTime = gameSettings->GameLoadStartTime;

	// create hero
	gameSettings->PlayerClients[id] = GAME_CLIENT_ID;
	gameSettings->PlayerSkins[id] = 0;
	gameSettings->PlayerTeams[id] = TargetTeam;

	/* Match the roster's local ordinal when remote human slots precede us. */
	int localSlot = 0, j;
	for (j = 0; j < id; ++j) {
		if (gameSettings->PlayerClients[j] == GAME_CLIENT_ID)
			++localSlot;
	}
	/* Net-data storage is indexed by roster ID, not local ordinal. A bot
	 * after remote humans needs the extended path even with a low ordinal. */
	int extra = id >= 4;
	if (extra)
		localSlot = BOT_SCRATCH_SLOT;
	sPlayer->Pad.id = localSlot;
	u32 savedPad = *(u32*)(BOT_PAD_LOCATION + (4 * localSlot));
	POKE_U32(BOT_PAD_LOCATION + (4 * localSlot), (u32)&sPlayer->Pad);

	// Use the original retail constructor and its local-player storage.
	/* Online's network allocation can return before a simulated player has a
	 * body. These are local training actors, so construct them synchronously
	 * through the same path used by Local Play, then restore the session flag.
	 * startBots runs inside the loader's re-entry guard: nested SDK syncs must
	 * not dispatch offline modules while this scoped flag is set. */
	int onlyLocal = GAME_NET_INFO->onlyLocalPlayers;
	if (BotsOnline)
		GAME_NET_INFO->onlyLocalPlayers = 1;
	if (extra)
		createExtraPlayer(id);
	else
		((void (*)(int))GetAddress(&vaCreateHero))(localSlot);
	GAME_NET_INFO->onlyLocalPlayers = onlyLocal;

	Player* newPlayer = players[id];
	POKE_U32(BOT_PAD_LOCATION + (4 * localSlot), savedPad);
	if (!newPlayer) {
		gameSettings->PlayerClients[id] = saved->client;
		gameSettings->PlayerSkins[id] = saved->skin;
		gameSettings->PlayerTeams[id] = saved->team;
		return;
	}
	sPlayer->Player = newPlayer;
	sPlayer->vtable = playerGetVTable(sPlayer->Player);

	players[id] = sPlayer->Player;

	sPlayer->Player->pPad = (void*)&sPlayer->Pad;
	sPlayer->Player->fps.vars.cam_slot = localSlot;
	sPlayer->Player->mpIndex = id;
	sPlayer->Player->mpTeam = TargetTeam;
	sPlayer->Idx = idx;
	POKE_U32(sPlayer->Player->fps.vars.pHero, (u32)sPlayer->Player);

	// if (sPlayer->Player->pMoby) {
	// 	sPlayer->Player->pMoby->NetObject = sPlayer->Player;
	// 	*(u32*)((u32)sPlayer->Player->pMoby->PVar + 0x70) = (u32)sPlayer->Player;
	// }

	sPlayer->Active = 0;
	sPlayer->Created = 1;
	sPlayer->Extra = extra;
	/* A reused slot must be eligible for another native leave notification. */
	GAME_NET_INFO->unk_07e5[id] = 0;
}

void targetUpdate(simulatedPlayer_t* sPlayer)
{
	Player* player = playerGetFromSlot(0);
	if (!sPlayer || !player || !sPlayer->Active)
		return;

	// reset pad
	struct padButtonStatus* pad = (struct padButtonStatus*)sPlayer->Pad.rdata;
	pad->btns = 0xffff;
	pad->ljoy_h = 0x7f;
	pad->ljoy_v = 0x7f;
	pad->circle_p = 0;
	pad->cross_p = 0;
	pad->square_p = 0;
	pad->triangle_p = 0;
	pad->down_p = 0;
	pad->left_p = 0;
	pad->right_p = 0;
	pad->up_p = 0;
	pad->l1_p = 0;
	pad->l2_p = 0;
	pad->r1_p = 0;
	pad->r2_p = 0;

	// send to mode for custom logic
	modeUpdateTarget(sPlayer);
}

void spawnTarget(simulatedPlayer_t* sPlayer)
{
	if (!sPlayer)
		return;

	// create
	if (!sPlayer->Created)
		createSimPlayer(sPlayer, sPlayer->Idx);

	// destroy existing
	if (!sPlayer->Created)
		return;

	if (sPlayer->Active)
		playerSetHealth(sPlayer->Player, 0);

	if (sPlayer->Extra)
		scopedPlayer = sPlayer->Player;
	playerRespawn(sPlayer->Player);
	modeInitTarget(sPlayer);
	scopedPlayer = 0;
}

void onSimulateHeros(void)
{
	int i;
	if (!Initialized || !isInGame() || !gameGetSettings())
		return;
	Player* local = playerGetFromSlot(0);
	if (!local || !local->pMoby || !local->camera)
		return;

	// update local hero first
	// Player * lPlayer = playerGetFromSlot(0);
	// if (lPlayer) {
	// 	PlayerVTable * pVTable = playerGetVTable(lPlayer);
	// 	// HUD JAL: 004E3F14
	// 	pVTable->Update(lPlayer); // update hero
	// }

	// update remote clients
	for (i = 0; i < SimPlayerCount; ++i) {
		if (SimPlayers[i].Player && playerGetAll()[SimPlayers[i].Player->mpIndex] == SimPlayers[i].Player) {

			targetUpdate(&SimPlayers[i]);

			// process input (Correct Address though)
			// ((void (*)(u32))GetAddress(&vaPadProcessInput))(&SimPlayers[i].Pad);

			// update pad (THIS IS CORRECT! :D)
			((void (*)(struct PAD*, void*, int))GetAddress(&vaPAD_PadUpdate))(
			    &SimPlayers[i].Pad, SimPlayers[i].Pad.rdata, 0x14);
		}
	}
}

void initBots(void)
{
	int i;

	modeInitialize();

	// give a small delay before finalizing the initialization.
	// this helps prevent the slow loaders from desyncing

	if (startDelay > 0) {
		--startDelay;
		return;
	}

	memset(SimPlayers, 0, sizeof(simulatedPlayer_t) * SimPlayerCount);
	int aggression = State.AggroMode;
	memset(&State, 0, sizeof(State));
	State.AggroMode = aggression;

	State.TimeLastKill = timerGetSystemTime();
	State.InitializedTime = gameGetTime();

	// run update functions for SimHeroes
	updateHook = GetAddressImmediate(&vaInit_RunUpdateFunctions);
	if (*(volatile u32*)updateHook != 0x03e00008 || *(volatile u32*)(updateHook + 4) != 0x27bd0040)
		return;
	HOOK_J(updateHook, onSimulateHeros);
	FlushCache(0);
	FlushCache(2);
	// HOOK_JAL(0x003d29f0, onSimulateHeros); // This one removes hud and such.

	for (i = 0; i < SimPlayerCount; ++i) {
		SimPlayers[i].Idx = i;
	}

	Initialized = 1;
}

void frameTick(void)
{
	int i = 0;

	//
	for (i = 0; i < GAME_MAX_PLAYERS; ++i) {
		modeProcessPlayer(i);
	}

	// draw counter
	//snprintf(buf, 32, "%d/%d", State.TargetsDestroyed, TARGET_GOAL);
	//gfxScreenSpaceText(15, SCREEN_HEIGHT - 15, 1, 1, 0x80ffffff, buf, -1, 6);

	// compute time left
	// float secondsLeft = ((gameOptions->GameFlags.MultiplayerGameFlags.Timelimit * TIME_MINUTE) - (nowTime - gameData->TimeStart)) / (float)TIME_SECOND;
	// if (secondsLeft < 0)
	// 	secondsLeft = 0;
	// int secondsLeftInt = (int)secondsLeft;
	// float timeSecondsRounded = secondsLeftInt;
	// if ((secondsLeft - timeSecondsRounded) > 0.5)
	// 	timeSecondsRounded += 1;

	// draw timer
	// if (shouldDrawHud()) {
	// 	sprintf(buf, "%02d:%02d", secondsLeftInt/60, secondsLeftInt%60);
	// 	gfxScreenSpaceText(479+1, 57+1, 0.8, 0.8, 0x80000000, buf, -1, 1);
	// 	gfxScreenSpaceText(479, 57, 0.8, 0.8, 0x80ffffff, buf, -1, 1);
	// }

	modeTick();
}

void gameTick(void)
{
	int i;
	for (i = 0; i < SimPlayerCount; ++i) {
		if (SimPlayers[i].Removed)
			continue;
		if (!SimPlayers[i].Active || playerIsDead(SimPlayers[i].Player)) {
			spawnTarget(&SimPlayers[i]);
			// if (!playerGetRespawnTimer(SimPlayers[i].Player)) {
			// 	spawnTarget(&SimPlayers[i]);
			// }
		}
	}
}

static int notificationFeed(botNotifications_t* feed)
{
	u32* leave = (u32*)GetAddressImmediate(&vaPlayerLeave);
	if (!leave || (leave[0x88 / 4] & 0xfc000000) != 0x0c000000)
		return 0;
	/* Use the same eight-entry notification feed as native player leaves.
	 * Its instruction layout was checked in all supported map overlays. */
	u32* notification = (u32*)((leave[0x88 / 4] & 0x03ffffff) << 2);
	if (notification[0] != 0x27bdffe0 || notification[1] != 0x24030030 ||
	    (notification[3] & 0xffff0000) != 0x3c020000 || notification[4] != 0x3c100025 ||
	    (notification[5] & 0xffff0000) != 0x8e100000 || (notification[6] & 0xffff0000) != 0x24420000 ||
	    notification[20] != 0x3c040025 || (notification[21] & 0xffff0000) != 0x24840000)
		return 0;
	feed->count = (int*)((notification[4] << 16) + (s16)notification[5]);
	feed->timers = (int*)((notification[20] << 16) + (s16)notification[21]);
	feed->messages = (char*)((notification[3] << 16) + (s16)notification[6]);
	int count = *feed->count;
	if ((u32)feed->count - (u32)feed->timers != 8 * sizeof(int) || count < 0 || count > 8)
		return 0;
	/* Native leaves assume space is available. Live kick/spawn cycles can
	 * fill the feed, so discard the oldest message before either event. */
	if (count == 8) {
		int i;
		for (i = 0; i < 7; ++i) {
			memcpy(feed->messages + i * 48, feed->messages + (i + 1) * 48, 48);
			feed->timers[i] = feed->timers[i + 1];
		}
		*feed->count = 7;
	}
	return 1;
}

static void notifyBotJoined(Player* player)
{
	botNotifications_t feed;
	GameSettings* settings = gameGetSettings();
	if (!settings || !player || !notificationFeed(&feed))
		return;
	int index = *feed.count;
	sprintf(feed.messages + index * 48, "%.15s joined the game.", settings->PlayerNames[player->mpIndex]);
	feed.timers[index] = 0x708;
	*feed.count = index + 1;
}

int botsSpawn(void)
{
	int i;
	if (!Initialized)
		return -1;
	for (i = 0; i < TRAINING_MAX_BOTS; ++i) {
		if (i < SimPlayerCount && SimPlayers[i].Created)
			continue;
		SimPlayers[i].Idx = i;
		SimPlayers[i].Removed = 0;
		spawnTarget(&SimPlayers[i]);
		if (!SimPlayers[i].Created) {
			SimPlayers[i].Removed = 1;
			return 0;
		}
		if (i >= SimPlayerCount)
			SimPlayerCount = i + 1;
		notifyBotJoined(SimPlayers[i].Player);
		return 1;
	}
	return 0;
}

int botsRemove(int index)
{
	u32* leave = (u32*)GetAddressImmediate(&vaPlayerLeave);
	GameSettings* settings = gameGetSettings();
	if (!Initialized || !settings || index < 0 || index >= SimPlayerCount || !leave || leave[0] != 0x27bdffd0 ||
	    leave[1] != 0x3c02001a || (leave[0xcc / 4] & 0xfc000000) != 0x0c000000 || leave[0xd0 / 4] != 0x0280202d)
		return 0;
	simulatedPlayer_t* bot = &SimPlayers[index];
	Player* player = bot->Player;
	if (!bot->Created || !player || player->mpIndex < 1 || player->mpIndex >= GAME_MAX_PLAYERS ||
	    playerGetAll()[player->mpIndex] != player || player->pPad != &bot->Pad || !player->pNetPlayer)
		return 0;
	int id = player->mpIndex;
	u32* net = (u32*)player->pNetPlayer;
	int wasLocal = player->isLocal;
	bot->Active = 0;
	botNotifications_t feed;
	notificationFeed(&feed);
	/* The native leave notification reads the cached network-player name.
	 * Local training construction doesn't populate this remote-player field. */
	memcpy((u8*)net + 0x1b0, settings->PlayerNames[id], 16);
	*((char*)net + 0x1bf) = 0;
	/* Invoke the native per-player departure, never client disconnection:
	 * all these local training actors share the host's client ID. */
	u32 clientLeave = leave[0xcc / 4];
	/* The final call migrates/deletes every object owned by the departing
	 * client. A bot leaves individually; its human host remains connected.
	 * Keep hero/equipment cleanup and notification, skip client teardown. */
	leave[0xcc / 4] = 0;
	flushCode();
	player->isLocal = 0;
	((void (*)(void*))leave)(net);
	player->isLocal = wasLocal;
	leave[0xcc / 4] = clientLeave;
	flushCode();
	if (playerGetAll()[id] == player) {
		bot->Active = 1;
		return 0;
	}
	/* Our local net data isn't a server-owned allocation. Release its slot
	 * without deleting a network object or disconnecting the host. */
	net[0] = (u32)-1;
	net[1] = 0;
	*(u16*)((u8*)net + 0x1a0) = 0;
	*(u16*)((u8*)net + 0x1a2) = 0;
	*(u16*)((u8*)net + 0x1a4) = 0;
	*(u16*)((u8*)net + 0x1a6) = 0;
	botSlot_t* saved = &savedSlots[index];
	settings->PlayerClients[id] = saved->client;
	settings->PlayerSkins[id] = saved->skin;
	settings->PlayerTeams[id] = saved->team;
	memcpy(settings->PlayerNames[id], saved->name, sizeof(saved->name));
	memset(bot, 0, sizeof(*bot));
	bot->Idx = index;
	bot->Removed = 1;
	return 1;
}

void startBots(void) // gameStart
{
	// How Many Local Players
	// *(u32*)0x001a5e5c = 3;

	// Ensure in game
	GameSettings* gameSettings = gameGetSettings();
	if (!gameSettings || !isInGame())
		return;

	if (!Initialized) {
		initBots(); // initialize()
		return;
	}
	frameTick();
	gameTick();

	/* Simulate extra local AI under the loader's re-entry guard. The local
	 * net-data path isn't a registered online client: suppress its native
	 * pad broadcasts without changing the session outside this call. */
	int i;
	for (i = 0; i < SimPlayerCount; ++i) {
		simulatedPlayer_t* bot = &SimPlayers[i];
		if (!bot->Extra || !bot->Active || !bot->Player || playerGetAll()[bot->Player->mpIndex] != bot->Player)
			continue;
		int onlyLocal = GAME_NET_INFO->onlyLocalPlayers;
		GAME_NET_INFO->onlyLocalPlayers = 1;
		scopedPlayer = bot->Player;
		bot->vtable->Update(bot->Player);
		/* These actors have no native local camera pass. Refresh the FPS
		 * position explicitly so Horizon spectates their current body, not
		 * the position cached before their first respawn. */
		u32* camera = (u32*)GetAddressImmediate(&vaSetFpsCamera);
		if (camera && camera[0] == 0x27bdffb0 && camera[1] == 0x44800800)
			((void (*)(Player*))camera)(bot->Player);
		scopedPlayer = 0;
		GAME_NET_INFO->onlyLocalPlayers = onlyLocal;
	}
}

/* Settings live in main.c. Only per-level objects and our owned hook reset. */
void botsReset(int multiplayer)
{
	/* Deactivation can arrive after the campaign ELF has replaced multiplayer.
	 * Its SDK stubs and old player objects are no longer callable then. */
	if (!multiplayer) {
		savedSettings = 0;
		getterHook = 0;
		scopedPlayer = 0;
		updateHook = 0;
		Initialized = 0;
		startDelay = 15;
		return;
	}
	if (updateHook && *(volatile u32*)updateHook == (0x08000000 | (((u32)onSimulateHeros >> 2) & 0x03ffffff)) &&
	    *(volatile u32*)(updateHook + 4) == 0x27bd0040) {
		*(volatile u32*)updateHook = 0x03e00008;
		FlushCache(0);
		FlushCache(2);
	}
	GameSettings* settings = gameGetSettings();
	if (settings && settings == savedSettings && settings->GameLoadStartTime == savedLoadTime) {
		int i;
		for (i = 0; i < TRAINING_MAX_BOTS; ++i) {
			botSlot_t* saved = &savedSlots[i];
			if (!SimPlayers[i].Created)
				continue;
			settings->PlayerClients[saved->id] = saved->client;
			settings->PlayerSkins[saved->id] = saved->skin;
			settings->PlayerTeams[saved->id] = saved->team;
			memcpy(settings->PlayerNames[saved->id], saved->name, sizeof(saved->name));
		}
	}
	savedSettings = 0;
	if (getterHook && *(u32*)getterHook == (0x08000000 | (((u32)botGetFromSlot >> 2) & 0x03ffffff)) &&
	    *(u32*)(getterHook + 4) == 0) {
		memcpy((void*)getterHook, getterOriginal, sizeof(getterOriginal));
		flushCode();
	}
	getterHook = 0;
	scopedPlayer = 0;
	updateHook = 0;
	Initialized = 0;
	startDelay = 15;
	memset(SimPlayers, 0, sizeof(simulatedPlayer_t) * TRAINING_MAX_BOTS);
}

int botsLevelChanged(void)
{
	return Initialized && (updateHook != (u32)GetAddressImmediate(&vaInit_RunUpdateFunctions) ||
	                       *(volatile u32*)updateHook != (0x08000000 | (((u32)onSimulateHeros >> 2) & 0x03ffffff)));
}
