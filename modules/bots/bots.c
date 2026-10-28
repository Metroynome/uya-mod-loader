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

#define BOT_PAD_LOCATION ((u32)PAD_POINTER + 0xc)

typedef struct botSlot {
	/* 0x00 */ int id;
	/* 0x04 */ short client;
	/* 0x06 */ short skin;
	/* 0x08 */ short team;
	/* 0x0a */ char name[16];
} botSlot_t;

void modeProcessPlayer(int pIndex);
void modeTick(void);
void modeInitTarget(simulatedPlayer_t* sPlayer);
void modeInitialize(void);
void FlushCache(int operation);
void modeUpdateTarget(simulatedPlayer_t* sPlayer);
void frameTick(void);
void gameTick(void);

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

int Initialized = 0;
int BotsOnline;
trainingState_t State;
static int startDelay = 15;
static u32 updateHook;
static botSlot_t savedSlots[3];
static GameSettings* savedSettings;
static int savedLoadTime;

extern int TargetTeam;
extern int SimPlayerCount;
extern simulatedPlayer_t SimPlayers[];

const char* TRAINING_AGGRO_NAMES[] = {
    [TRAINING_AGGRESSION_AGGRO] "AGGRO",
    [TRAINING_AGGRESSION_AGGRO_NO_DAMAGE] "NO DAMAGE",
    [TRAINING_AGGRESSION_PASSIVE] "PASSIVE",
    [TRAINING_AGGRESSION_IDLE] "IDLE",
};

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
	/* Humans and bots together must fit the retail four-local-hero limit. */
	if (localSlot >= 4) {
		gameSettings->PlayerClients[id] = saved->client;
		gameSettings->PlayerSkins[id] = saved->skin;
		gameSettings->PlayerTeams[id] = saved->team;
		return;
	}
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

	playerRespawn(sPlayer->Player);
	modeInitTarget(sPlayer);
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

			// run game's hero update
			// pVTable->Update(SimPlayers[i].Player);
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
		if (!SimPlayers[i].Active || playerIsDead(SimPlayers[i].Player)) {
			spawnTarget(&SimPlayers[i]);
			// if (!playerGetRespawnTimer(SimPlayers[i].Player)) {
			// 	spawnTarget(&SimPlayers[i]);
			// }
		}
	}
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
}

/* Settings live in main.c. Only per-level objects and our owned hook reset. */
void botsReset(int multiplayer)
{
	/* Deactivation can arrive after the campaign ELF has replaced multiplayer.
	 * Its SDK stubs and old player objects are no longer callable then. */
	if (!multiplayer) {
		savedSettings = 0;
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
		for (i = 0; i < 3; ++i) {
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
	updateHook = 0;
	Initialized = 0;
	startDelay = 15;
	memset(SimPlayers, 0, sizeof(simulatedPlayer_t) * 3);
}

int botsLevelChanged(void)
{
	return Initialized && (updateHook != (u32)GetAddressImmediate(&vaInit_RunUpdateFunctions) ||
	                       *(volatile u32*)updateHook != (0x08000000 | (((u32)onSimulateHeros >> 2) & 0x03ffffff)));
}
