#include <string.h>
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/net.h>
#include <libuya/stdio.h>
#include <libuya/uya.h>
#include "training/training.h"
#include "online.h"

#if UYA_PAL
#define BOT_NET_CLASS_NAME   (0x00241678)
#define BOT_NET_CLASS_INDEX  (0x0024063c)
#define BOT_NET_WORLD        (0x001d8788)
#define BOT_NET_OBJECT_FLAGS (0x001d87b0)
#define BOT_NET_REPORT       (0x00196468)
#else
#define BOT_NET_CLASS_NAME   (0x002417f8)
#define BOT_NET_CLASS_INDEX  (0x002407bc)
#define BOT_NET_WORLD        (0x001d8908)
#define BOT_NET_OBJECT_FLAGS (0x001d8930)
#define BOT_NET_REPORT       (0x00196558)
#endif

/* Prefix of the native DME object descriptor (32-bit EE pointers). */
typedef struct botNetworkObject {
	/* 0x00 */ u8 header[4];
	/* 0x04 */ int creator;
	/* 0x08 */ int owner;
	/* 0x0c */ char name[16];
	/* 0x1c */ void* data;
} botNetworkObject_t;

int BotsNetwork;
/* Each published guest owns one native network object. */
static int networkBot = -1;
static int networkBotCount;
static int networkBotMask;
static u32 networkObjects[TRAINING_MAX_BOTS];
static int networkError;

int botsNetworkActive(void)
{
	return networkBotCount;
}

static void publishBotRoster(int id)
{
	GameSettings* settings = gameGetSettings();
	int (*sendObject)(int, u32) = (void*)0x0016ef40;
	/* Publish directly to each human connection. The native broadcast path
	 * marks the local shadow as sent even when transport delivery fails;
	 * directed sends also let us retain the transport's return code. */
	int i, j;
	for (i = 0; i < GAME_MAX_PLAYERS; ++i) {
		int client = settings->PlayerClients[i];
		if (client < 0 || client == GAME_CLIENT_ID)
			continue;
		for (j = 0; j < i; ++j)
			if (settings->PlayerClients[j] == client)
				break;
		if (j != i)
			continue;
		int rosterResult = sendObject(client, *(u32*)((u8*)settings + 0x1a0));
		int playerResult = id >= 0 ? sendObject(client, id) : 0;
		networkError = rosterResult ? rosterResult : playerResult;
		printf("bots: publish to client %d roster=%d player=%d\n", client, rosterResult, playerResult);
	}
	((void (*)(void))BOT_NET_REPORT)();
}

int botsOnlinePublish(int index, Player* player)
{
	/* Resident networking routines match at the same code addresses in NTSC-U
	 * and PAL; class/report/world data references above are region-specific.
	 * The last two arguments are passed on the EE stack, not in registers.
	 * Do not call NetSetMyClientObject: the host keeps its original player. */
	int (*createObject)(u32*, int, int, int, int, int, int, const void*, const char*, u32) = (void*)0x0016ec18;
	int (*getObject)(botNetworkObject_t**, u32) = (void*)0x0016ed00;
	void (*releaseObject)(u32) = (void*)0x0016edd0;
	GameSettings* settings = gameGetSettings();
	botNetworkObject_t* object = 0;
	u8 data[0x90] __attribute__((aligned(16)));
	u32 id;
	if (!settings || !BotsOnline || !gameAmIHost() || !player || !player->pNetPlayer ||
	    !player->pNetPlayer->pNetPlayerData)
		return 0;
	/* The native registered payload is 0x90 bytes; older SDK headers describe
	 * a larger structure. Copy the verified native size only. */
	memcpy(data, player->pNetPlayer->pNetPlayerData, sizeof(data));
	/* A mid-game guest misses the warp sent during local construction. Give
	 * its receiver the current spawn, not the constructor's initial position. */
	memcpy(data, player->playerPosition, 3 * sizeof(float));
	memcpy(data + 0x10, player->playerRotation, 3 * sizeof(float));
	*(int*)(data + 0x1c) = gameGetTime();
	*(int*)(data + 0x34) = -1; /* Guest account, as in native host split-screen. */
	*(int*)(data + 0x54) = player->mpIndex;
	networkError = createObject(&id, 1, 2, 0, 0, 0, 0, data, (const char*)BOT_NET_CLASS_NAME, *(u32*)BOT_NET_CLASS_INDEX);
	/* Native clients reserve only three object IDs: settings plus two heroes.
	 * The session master can reserve another unused ID with the same allocator
	 * used when accepting a client. Do not enlarge into another client's range.
	 * Keep the reservation and creation under the native network mutex, and
	 * restore the host's normal range before releasing it. */
	if (networkError == 0x13 && ((int (*)(void))0x00150550)() == 0) {
		void* world = *(void**)BOT_NET_WORLD;
		u32* client = ((void* (*)(void*, int))0x0014a490)(world, GAME_CLIENT_ID);
		int extraId = client ? ((int (*)(int))0x00173ed8)(1) : -1;
		if (extraId >= 0 && extraId < 0x1000) {
			u32 first = client[2];
			u32 count = client[3];
			client[2] = extraId;
			client[3] = 1;
			/* Call the unlocked implementation while holding the mutex. */
			createObject = (void*)0x0016fd68;
			networkError = createObject(&id, 1, 2, 0, 0, 0, 0, data, (const char*)BOT_NET_CLASS_NAME, *(u32*)BOT_NET_CLASS_INDEX);
			client[2] = first;
			client[3] = count;
			if (networkError)
				((u8*)BOT_NET_OBJECT_FLAGS)[extraId] = 0;
		}
		((int (*)(void))0x00150590)();
	}
	if (networkError) {
		printf("bots: network creation failed (%d)\n", networkError);
		return 0;
	}
	networkError = getObject(&object, id);
	if (networkError || !object || !object->data) {
		printf("bots: network object lookup failed (%d)\n", networkError);
		releaseObject(id);
		return 0;
	}
	player->pNetPlayer->pNetPlayerData = object->data;
	int slot = player->mpIndex;
	settings->PlayerAccountIds[slot] = -1;
	settings->PlayerStates[slot] = settings->PlayerStates[0];
	settings->PlayerTypes[slot] = GS_PLAYER_TYPE_NOBDY;
	settings->PlayerRanks[slot] = settings->PlayerRanks[0];
	settings->PlayerRankDeviations[slot] = settings->PlayerRankDeviations[0];
	settings->PlayerCount += 1;
	/* Keep numPlayers in sync, without increasing the real local-player
	 * count (which would enable additional controller/HUD/viewport passes). */
	GAME_NET_INFO->numPlayers = settings->PlayerCount;
	networkBot = (int)id;
	networkBotCount += 1;
	networkBotMask |= 1 << index;
	networkObjects[index] = id;
	publishBotRoster(id);
	printf("bots: player %d published as host-owned object %d\n", slot, networkBot);
	return 1;
}

int botsOnlinePublished(int index)
{
	return BotsOnline && (networkBotMask & (1 << index));
}

void botsOnlineRemove(int index)
{
	/* Delete only this owned object. The native path broadcasts its ID and
	 * runs the registered data-deletion callback on each receiving client. */
	int (*lock)(void) = (void*)0x00150550;
	int (*unlock)(void) = (void*)0x00150590;
	if (lock() == 0) {
		networkError = ((int (*)(u32))0x0016f510)(networkObjects[index]);
		unlock();
	} else {
		networkError = 0x2b;
	}
	networkBotMask &= ~(1 << index);
	networkObjects[index] = 0;
	networkBotCount -= 1;
}

void botsOnlineRosterChanged(void)
{
	publishBotRoster(-1);
}

int botsOnlineReset(void)
{
	int published = networkBotCount;
	networkBotCount = 0;
	networkBotMask = 0;
	memset(networkObjects, 0, sizeof(networkObjects));
	networkBot = -1;
	networkError = 0;
	return published;
}

void botsOnlinePad(struct PAD* pad)
{
	/* Simulated pads bypass hardware polling, which normally feeds the
	 * compressed network stream before the native PAD update. */
	if (BotsOnline && botsNetworkActive() && pad->RawPadInputCallback) {
		((void (*)(struct PAD*, void*, int, void*))pad->RawPadInputCallback)(
		    pad, pad->rdata, 0x14, pad->pCallbackData);
	}
}
