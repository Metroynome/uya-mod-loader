/* Ported from DL Forge Free Cam, adapted from Horizon's patch/freecam.c by Daniel "Dnawrkshp" Gerendasy
 * and Connor "Badger41" Williams. Local Play only; no patch/DZO globals. */
#include "freecam.h"
#include <libuya/pad.h>
#include <libuya/interop.h>
#include <libuya/camera.h>
#include <libuya/game.h>
#include <libuya/gamesettings.h>
#include <libuya/graphics.h>
#include <libuya/math.h>
#include <libuya/player.h>
#include <libuya/string.h>

#define WORD(address) (*(volatile u32*)(address))
#if UYA_MODULE_REGION == 2
#define LEVEL_ID (0x1f83a8)
#else
#define LEVEL_ID (0x1f8528)
#endif
#define MOVE_SPEED (0.5f)
#define FAST_MOVE_SPEED (2.0f)
#define SLOW_MOVE_SPEED (0.12f)
#define RETICLE_CALL_OFFSET (0x15b0)
#define RETICLE_DRAW_OFFSET (0xee0)

/* EE pointers are 32-bit; VECTOR members have 16-byte alignment. */
typedef struct freecamPlayer {
	/* 0x00 */ Player* player;
	/* 0x04 */ int enabled;
	/* 0x08 */ int controlCharacter;
	/* 0x0c */ int ignoreCharacterCamera;
	/* 0x10 */ int hideControls;
	/* 0x14 */ int lockState;
	/* 0x18 */ u32 buttons;
	/* 0x1c */ u32 reserved;
	/* 0x20 */ VECTOR position;
	/* 0x30 */ VECTOR lockedPosition;
	/* 0x40 */ VECTOR bookmark;
	/* 0x50 */ float yaw;
	/* 0x54 */ float pitch;
	/* 0x58 */ float characterYaw;
	/* 0x5c */ float characterPitch;
	/* 0x60 */ float cameraOffset;
	/* 0x64 */ int noInput;
	/* 0x68 */ float bookmarkYaw;
	/* 0x6c */ float bookmarkPitch;
	/* 0x70 */ int bookmarkValid;
} freecamPlayer_t;

static void cameraPost(void);
static void hudUpdate(void);

/* Camera render and update epilogues verified in all ten retail overlays.
 * Custom maps use the addresses of their retail base through libuya. */
#if UYA_MODULE_REGION == 2
static VariableAddress_t cameraRender = {
	.Bakisi = 0x447960, .Hoven = 0x4494e0, .OutpostX12 = 0x4402e0,
	.KorgonOutpost = 0x43dea0, .Metropolis = 0x43d1e0, .BlackwaterCity = 0x43a9e0,
	.CommandCenter = 0x43b660, .BlackwaterDocks = 0x43dee0,
	.AquatosSewers = 0x43d1e0, .MarcadiaPalace = 0x43cb60
};
#else
static VariableAddress_t cameraRender = {
	.Bakisi = 0x446ab8, .Hoven = 0x448578, .OutpostX12 = 0x43f3b8,
	.KorgonOutpost = 0x43cff8, .Metropolis = 0x43c338, .BlackwaterCity = 0x439ab8,
	.CommandCenter = 0x43a8f8, .BlackwaterDocks = 0x43d138,
	.AquatosSewers = 0x43c478, .MarcadiaPalace = 0x43bdb8
};
#endif
/* The native HUD update has a pause-only path that hides its widgets.
 * These call sites and the pause test were checked in both regional extracts. */
#if UYA_MODULE_REGION == 2
#define HUD_PROLOGUE (0x27bdff70)
static VariableAddress_t hudUpdateCall = {
	.Bakisi = 0x4eecc4, .Hoven = 0x4f0ddc, .OutpostX12 = 0x4e66b4,
	.KorgonOutpost = 0x4e3e4c, .Metropolis = 0x4e319c, .BlackwaterCity = 0x4e0a34,
	.CommandCenter = 0x4e09fc, .BlackwaterDocks = 0x4e327c,
	.AquatosSewers = 0x4e257c, .MarcadiaPalace = 0x4e1efc
};
#else
#define HUD_PROLOGUE (0x27bdff60)
static VariableAddress_t hudUpdateCall = {
	.Bakisi = 0x4ec5a4, .Hoven = 0x4ee5fc, .OutpostX12 = 0x4e3f14,
	.KorgonOutpost = 0x4e172c, .Metropolis = 0x4e0a7c, .BlackwaterCity = 0x4de294,
	.CommandCenter = 0x4de41c, .BlackwaterDocks = 0x4e0c5c,
	.AquatosSewers = 0x4dff9c, .MarcadiaPalace = 0x4df8dc
};
#endif
extern VariableAddress_t vaOcclusionAddr;

freecamSettings_t forgeFreecam;
static const moduleApi_t* loader;
static freecamPlayer_t players[GAME_MAX_LOCALS];
static int level = -1, lastTime = -1, patched;
static u32 originalOcclusion, changedOcclusion;
static int occlusionChanged;
static u32 cameraPostAddress, cameraRenderAddress, occlusionAddress;
static u32 hudHookAddress, hudOriginal, hudUpdateAddress;
static u32 reticleCallAddress, reticleOriginal;

static void flush(void)
{
	loader->flushCache(0);
	loader->flushCache(2);
}

int forgeFreecamAvailable(void)
{
	return loader && loader->mode == UYA_MODE_MULTIPLAYER && loader->context == UYA_CONTEXT_LOCAL &&
	       gameGetSettings() && isInGame();
}

static Player* localPlayer(int index)
{
	int i;
	for (i = 0; i < GAME_MAX_PLAYERS; ++i) {
		Player* player = playerGetFromSlot(i);
		if (player && player->isLocal && player->fps.vars.cam_slot == index)
			return player;
	}
	return 0;
}

static int localPlayerCount(void)
{
	return (localPlayer(0) != 0) + (localPlayer(1) != 0);
}

/* The temporary pause value is visible only to this synchronous HUD call;
 * gameplay, input polling and the real pause menu keep their original values. */
static void hudUpdate(void)
{
	Player* local[GAME_MAX_LOCALS] = {0};
	u8 paused[GAME_MAX_LOCALS] = {0};
	int i;
	if (forgeFreecamAvailable() && *(volatile int*)LEVEL_ID == level) {
		for (i = 0; i < GAME_MAX_LOCALS; ++i) {
			freecamPlayer_t* data = &players[i];
			Player* player = localPlayer(i);
			if (!data->enabled || player != data->player)
				continue;
			local[i] = player;
			paused[i] = player->pauseOn;
			player->pauseOn = 1;
		}
	}
	((void (*)(void))hudUpdateAddress)();
	for (i = 0; i < GAME_MAX_LOCALS; ++i)
		if (local[i])
			local[i]->pauseOn = paused[i];
}

static void rotation(MATRIX matrix, const freecamPlayer_t* data)
{
	matrix_unit(matrix);
	matrix_rotate_y(matrix, matrix, data->pitch);
	matrix_rotate_z(matrix, matrix, data->yaw);
}

static void cameraPost(void)
{
	int i;
	if (!forgeFreecamAvailable() || *(volatile int*)LEVEL_ID != level)
		return;
	/* The native camera update resets this flag each frame. */
	if (occlusionChanged)
		WORD(occlusionAddress) = changedOcclusion;
	for (i = 0; i < GAME_MAX_LOCALS; ++i) {
		freecamPlayer_t* data = &players[i];
		GameCamera* camera;
		MATRIX matrix, original;
		if (!data->enabled || localPlayer(i) != data->player)
			continue;
		camera = cameraGetGameCamera(i);
		memcpy(original, &camera->uMtx, sizeof(float) * 12);
		vector_copy(camera->pos, data->position);
		camera->rot[0] = 0;
		camera->rot[1] = data->pitch;
		camera->rot[2] = data->yaw;
		rotation(matrix, data);
		memcpy(&camera->uMtx, matrix, sizeof(float) * 12);
		((void (*)(int))cameraRenderAddress)(i);
		if (data->ignoreCharacterCamera)
			memcpy(&camera->uMtx, original, sizeof(float) * 12);
	}
}

static void restoreHooks(void)
{
	u32 jump = 0x08000000 | ((u32)cameraPost >> 2);
	if (!patched)
		return;
	if (occlusionChanged && WORD(occlusionAddress) == changedOcclusion)
		WORD(occlusionAddress) = originalOcclusion;
	occlusionChanged = 0;
	if (WORD(hudHookAddress) == (0x0c000000 | ((u32)hudUpdate >> 2)))
		WORD(hudHookAddress) = hudOriginal;
	/* Overlay replacement can reuse these addresses. Restore only our words. */
	if (WORD(cameraPostAddress) == jump && WORD(cameraPostAddress + 4) == 0x27bd0080) {
		WORD(cameraPostAddress) = 0x03e00008;
		if (!WORD(reticleCallAddress))
			WORD(reticleCallAddress) = reticleOriginal;
		flush();
	}
	patched = 0;
}

static int installHooks(void)
{
	if (patched)
		return WORD(cameraPostAddress) == (0x08000000 | ((u32)cameraPost >> 2));
	cameraRenderAddress = GetAddress(&cameraRender);
	occlusionAddress = GetAddress(&vaOcclusionAddr);
	if (!cameraRenderAddress || !occlusionAddress)
		return 0;
	hudHookAddress = GetAddress(&hudUpdateCall);
	if (!hudHookAddress)
		return 0;
	hudOriginal = WORD(hudHookAddress);
	hudUpdateAddress = (hudOriginal & 0x03ffffff) << 2;
	if ((hudOriginal >> 26) != 3 || WORD(hudUpdateAddress) != HUD_PROLOGUE ||
	    WORD(hudUpdateAddress + 0x54) != 0x92022518)
		return 0;
	/* The shared reticle call has the same relative offset in all twenty
	 * verified regional overlays. Suppress it only while a camera is active. */
	reticleCallAddress = cameraRenderAddress + RETICLE_CALL_OFFSET;
	reticleOriginal = 0x0c000000 | ((cameraRenderAddress + RETICLE_DRAW_OFFSET) >> 2);
	if (WORD(reticleCallAddress) != reticleOriginal ||
	    WORD(cameraRenderAddress + RETICLE_DRAW_OFFSET) != 0x27bdff90)
		return 0;
	cameraPostAddress = cameraRenderAddress + 0x40c;
	if (WORD(cameraRenderAddress) != 0x27bdffd0 ||
	    WORD(cameraPostAddress) != 0x03e00008 || WORD(cameraPostAddress + 4) != 0x27bd0080)
		return 0;
	WORD(cameraPostAddress) = 0x08000000 | ((u32)cameraPost >> 2);
	WORD(hudHookAddress) = 0x0c000000 | ((u32)hudUpdate >> 2);
	WORD(reticleCallAddress) = 0;
	/* Keep occlusion updates running so Render All rebuilds the terrain list. */
	originalOcclusion = WORD(occlusionAddress);
	occlusionChanged = 0;
	patched = 1;
	flush();
	return 1;
}

static void disablePlayer(int index)
{
	freecamPlayer_t* data = &players[index];
	Player* player = data->player;
	/* Never restore saved values into a new level/player allocation. */
	if (data->enabled && forgeFreecamAvailable() && *(volatile int*)LEVEL_ID == level && localPlayer(index) == player) {
		player->timers.noInput = data->noInput;
		player->fps.vars.positionOffset[0] = data->cameraOffset;
		player->fps.vars.cameraZ.rotation = data->characterYaw;
		player->fps.vars.cameraY.rotation = data->characterPitch;
	}
	data->enabled = 0;
	data->player = 0;
	data->lockState = -1;
}

void forgeFreecamReset(void)
{
	int i;
	for (i = 0; i < GAME_MAX_LOCALS; ++i)
		disablePlayer(i);
	restoreHooks();
	memset(players, 0, sizeof(players));
	level = lastTime = -1;
}

void forgeFreecamInit(const moduleApi_t* api)
{
	loader = api;
	memset(&forgeFreecam, 0, sizeof(forgeFreecam));
	forgeFreecam.enabled = 1;
	memset(players, 0, sizeof(players));
}

static int enablePlayer(Player* player, freecamPlayer_t* data)
{
	if (!installHooks()) {
		loader->print("forge: freecam unavailable; camera hooks are not retail\n");
		return 0;
	}
	data->player = player;
	data->enabled = 1;
	data->controlCharacter = data->ignoreCharacterCamera = data->hideControls = 0;
	data->lockState = -1;
	data->yaw = data->characterYaw = player->fps.vars.cameraZ.rotation;
	data->pitch = data->characterPitch = player->fps.vars.cameraY.rotation;
	data->cameraOffset = player->fps.vars.positionOffset[0];
	data->noInput = player->timers.noInput;
	vector_copy(data->position, player->fps.cameraPos);
	vector_copy(data->lockedPosition, player->playerPosition);
	return 1;
}

static float axis(u8 value)
{
	if (value > 143)
		return (value - 143) / 112.0f;
	if (value < 111)
		return (value - 111) / 111.0f;
	return 0;
}

static void moveCamera(freecamPlayer_t* data, const PAD* pad, u32 buttons)
{
	MATRIX matrix;
	VECTOR direction;
	float movement[2] = {axis(pad->ljoy_v), axis(pad->ljoy_h)};
	float speed = MOVE_SPEED;
	if ((buttons & (PAD_L1 | PAD_R1)) == PAD_L1)
		speed = FAST_MOVE_SPEED;
	else if ((buttons & (PAD_L1 | PAD_R1)) == PAD_R1)
		speed = SLOW_MOVE_SPEED;
	int i;
	/* Use native look input, including camera sensitivity and inversion. */
	data->yaw = data->player->fps.vars.cameraZ.rotation;
	data->pitch = data->player->fps.vars.cameraY.rotation;
	rotation(matrix, data);
	for (i = 0; i < 2; ++i) {
		vector_copy(direction, &matrix[i * 4]);
		if (buttons & PAD_L3) {
			direction[2] = 0;
			if (vector_sqrmag(direction) < 0.0001f)
				continue;
			vector_normalize(direction, direction);
		}
		vector_scale(direction, direction, -movement[i] * speed);
		vector_add(data->position, data->position, direction);
	}
	if (buttons & PAD_L2)
		data->position[2] -= speed;
	if (buttons & PAD_R2)
		data->position[2] += speed;

}

static void controls(const freecamPlayer_t* data, int index)
{
	float y = SCREEN_HEIGHT - 8;
	float x = 8;
	float scale = localPlayerCount() > 1 ? 0.65f : 0.8f;
	if (!index && localPlayerCount() > 1)
		y = SCREEN_HEIGHT / 2 - 8;
	gfxScreenSpaceText(x, y, scale, scale, 0x80ffffff, "\x1c Controls   L1+\x1d Exit   Select+\x12 Save / \x13 Restore", -1, TEXT_ALIGN_BOTTOMLEFT, 0);
	gfxScreenSpaceText(x, y - 15, scale, scale, 0x80ffffff,
	    data->controlCharacter ? "\x1a Lock Character" : "\x1a Control Character", -1, TEXT_ALIGN_BOTTOMLEFT, 0);
	gfxScreenSpaceText(x, y - 30, scale, scale, 0x80ffffff,
	    data->ignoreCharacterCamera ? "\x1b Use Character Camera" : "\x1b Ignore Character Camera", -1, TEXT_ALIGN_BOTTOMLEFT, 0);
	gfxScreenSpaceText(x, y - 45, scale, scale, 0x80ffffff,
	    (occlusionChanged ? changedOcclusion : WORD(occlusionAddress)) == 2 ? "\x13 Occlusion ON" : "\x13 Occlusion OFF (Render All)", -1, TEXT_ALIGN_BOTTOMLEFT, 0);
	if (forgeFreecam.lockAnimation)
		gfxScreenSpaceText(x, y - 60, scale, scale, 0x80ffffff,
		    data->lockState < 0 ? "\x1d Lock Animation" : "\x1d Release Animation", -1, TEXT_ALIGN_BOTTOMLEFT, 0);
}

static void updatePlayer(Player* player, freecamPlayer_t* data, const PAD* pad, u32 buttons, u32 pressed)
{
	if (pressed & PAD_DOWN)
		data->hideControls = !data->hideControls;
	if (pressed & PAD_RIGHT)
		data->ignoreCharacterCamera = !data->ignoreCharacterCamera;
	/* Select + Square restores the bookmark instead of toggling occlusion. */
	if ((pressed & PAD_SQUARE) && !(buttons & PAD_SELECT)) {
		changedOcclusion = (occlusionChanged ? changedOcclusion : WORD(occlusionAddress)) == 2 ? 0 : 2;
		WORD(occlusionAddress) = changedOcclusion;
		occlusionChanged = 1;
	}
	if ((buttons & PAD_SELECT) && (pressed & PAD_TRIANGLE)) {
		vector_copy(data->bookmark, data->position);
		data->bookmarkYaw = data->yaw;
		data->bookmarkPitch = data->pitch;
		data->bookmarkValid = 1;
	} else if ((buttons & PAD_SELECT) && (pressed & PAD_SQUARE) && data->bookmarkValid) {
		vector_copy(data->position, data->bookmark);
		data->yaw = data->bookmarkYaw;
		data->pitch = data->bookmarkPitch;
		player->fps.vars.cameraZ.rotation = data->yaw;
		player->fps.vars.cameraY.rotation = data->pitch;
	}
	if (pressed & PAD_LEFT) {
		data->controlCharacter = !data->controlCharacter;
		if (!data->controlCharacter) {
			data->characterYaw = player->fps.vars.cameraZ.rotation;
			data->characterPitch = player->fps.vars.cameraY.rotation;
		} else {
			player->fps.vars.cameraZ.rotation = data->characterYaw;
			player->fps.vars.cameraY.rotation = data->characterPitch;
		}
	}
	if (forgeFreecam.lockPosition) {
		vector_copy(player->playerPosition, data->lockedPosition);
	} else {
		vector_copy(data->lockedPosition, player->playerPosition);
	}
	if (!forgeFreecam.lockAnimation)
		data->lockState = -1;
	else if (pressed & PAD_UP)
		data->lockState = data->lockState < 0 ? player->state : -1;
	if (data->lockState >= 0 && player->state != data->lockState)
		((PlayerVTable*)player->guber.Vtable)->UpdateState(player, data->lockState, 1, 1, 1);
	player->fps.vars.positionOffset[0] = 0;
	player->timers.noInput = data->controlCharacter ? 0 : 0x7fff;
	if (!data->controlCharacter)
		moveCamera(data, pad, buttons);
	if (!data->hideControls)
		controls(data, player->fps.vars.cam_slot);
}

void forgeFreecamUpdate(int menuOpen, int loading)
{
	int i, any = 0, time;
	if (!forgeFreecamAvailable() || loading || *(volatile int*)LEVEL_ID != level) {
		forgeFreecamReset();
		if (forgeFreecamAvailable())
			level = *(volatile int*)LEVEL_ID;
		return;
	}
	if (!forgeFreecam.enabled) {
		for (i = 0; i < GAME_MAX_LOCALS; ++i)
			disablePlayer(i);
		restoreHooks();
		return;
	}
	if (occlusionChanged)
		WORD(occlusionAddress) = changedOcclusion;
	time = gameGetTime();
	if (time == lastTime)
		return;
	lastTime = time;
	for (i = 0; i < GAME_MAX_LOCALS; ++i) {
		freecamPlayer_t* data = &players[i];
		Player* player = localPlayer(i);
		PAD* pad = player ? playerGetPad(player) : 0;
		u32 buttons = pad ? (u16)~pad->btns : 0;
		u32 pressed = buttons & ~data->buttons;
		data->buttons = buttons;
		if (!player || player != data->player || playerIsDead(player))
			disablePlayer(i);
		if (!player || !pad || playerIsDead(player) || menuOpen || player->pauseOn || player->pauseTimer) {
			any |= data->enabled;
			continue;
		}
		if (forgeFreecam.airwalk) {
			player->ground.offAny = 0;
			player->ground.offGood = 0;
		}
		/* Circle belongs to Forge's reload combo; it must not toggle freecam. */
		if ((buttons & (PAD_L1 | PAD_UP | PAD_CIRCLE)) == (PAD_L1 | PAD_UP) && (pressed & (PAD_L1 | PAD_UP))) {
			if (data->enabled)
				disablePlayer(i);
			else
				enablePlayer(player, data);
		} else if (data->enabled) {
			updatePlayer(player, data, pad, buttons, pressed);
		}
		any |= data->enabled;
	}
	if (!any)
		restoreHooks();
}
