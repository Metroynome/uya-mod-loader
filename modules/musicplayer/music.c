#include "musicplayer.h"

#define WORD(address) (*(volatile u32*)((u32)(address) | 0x20000000))
#define JAL(address) (0x0c000000 | (((u32)(address) >> 2) & 0x03ffffff))
#define JUMP(address) (0x08000000 | (((u32)(address) >> 2) & 0x03ffffff))

typedef struct musicOverlay {
	/* 0x00 */ int level;
	/* 0x04 */ u32 play;
	/* 0x08 */ u32 queue;
	/* 0x0c */ u32 transition;
} musicOverlay_t;

/* Resident transition fields immediately before the main music stream. */
typedef struct musicTransition {
	/* 0x00 */ s16 state;
	/* 0x02 */ s16 padding;
	/* 0x04 */ int length;
	/* 0x08 */ int fadeTime;
	/* 0x0c */ int delay;
	/* 0x10 */ s16 track;
	/* 0x12 */ s16 type;
	/* 0x14 */ u8 readMode[4];
} musicTransition_t;

static int ignoreTrack(void);
static void streamStopped(int result, u64 context);
static void startupCommand(int command, int size, void* data, void (*callback)(int, u64), u64 context);

#include "addresses.h"

static const musicOverlay_t* current;
static const musicOverlay_t* owner;
static u32 ownerMode;
static u32 originalHooks[3][2];
static s16 savedTrack, savedFlags;
static s16 savedQueuedTrack, savedQueuedType;
static u32 ownerGeneration;
static int ownerLevel = -2, stopping;
static int nativeBlocked;
static int startupPending, startupLevel = -1;

/*
 * NAME : musicText
 * DESCRIPTION : Format a track label without binding to either ELF's libc.
 * NOTES : Output has room for 64 bytes; number zero omits the number.
 * ARGS : Output, base name, optional number, and suffix.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicText(char* output, const char* name, int number, const char* suffix)
{
	int n = 0, digits = 0;
	char text[12];
	while (*name && n < 51)
		output[n++] = *name++;
	if (number > 0) {
		output[n++] = ' ';
		do {
			text[digits++] = '0' + number % 10;
			number /= 10;
		} while (number);
		while (digits)
			output[n++] = text[--digits];
	}
	while (*suffix && n < 63)
		output[n++] = *suffix++;
	output[n] = 0;
}

/*
 * NAME : musicBackendLevel
 * DESCRIPTION : Read the map ID from the active game's resident core.
 * NOTES :
 * ARGS :
 * RETURN : Native map ID, including -1 for the campaign boot menu.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendLevel(void)
{
	if (musicLoader->mode == UYA_MODE_CAMPAIGN)
		return *(volatile int*)0x1d545c;
#if UYA_MODULE_REGION == 2
	return *(volatile int*)0x1f83a8;
#else
	return *(volatile int*)0x1f8528;
#endif
}

/*
 * NAME : musicBackendContext
 * DESCRIPTION : Identify multiplayer lobby, staging, or gameplay music.
 * NOTES : Child menus retain the previous choice; campaign is not overridden.
 * ARGS :
 * RETURN : 0 lobby, 1 staging, 2 game, or -1 outside these screens.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendContext(void)
{
	int level = musicBackendLevel();
	u32 i, active = 0;
	if (musicLoader->mode != UYA_MODE_MULTIPLAYER)
		return -1;
	/* Scene readiness is checked by the caller. The panorama precedes GAME_ACTIVE. */
	if (level >= 40 && level <= 49)
		return 2;
	if (level != 39)
		return -1;
	/* These menu tables are shared by the NTSC-U and PAL lobby overlays. */
	for (i = 3; i > 0; --i) {
		active = WORD(0x1c5c110 + (i - 1) * 4);
		if (active)
			break;
	}
	if (!active)
		return -1;
	if (active == WORD(0x1c5c064 + 23 * 4) || active == WORD(0x1c5c064 + 33 * 4))
		return 1;
	return active == WORD(0x1c5c064 + 25 * 4) ? 0 : -1;
}

/*
 * NAME : musicBackendSector
 * DESCRIPTION : Read the native music descriptor base sector.
 * NOTES : Campaign and multiplayer keep different resident tables.
 * ARGS :
 * RETURN : Disc sector.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 musicBackendSector(void)
{
	if (musicLoader->mode == UYA_MODE_CAMPAIGN)
		return WORD(0x168644);
#if UYA_MODULE_REGION == 2
	return WORD(0x1f8404);
#else
	return WORD(0x1f8584);
#endif
}

/*
 * NAME : musicBackendTable
 * DESCRIPTION : Return this ELF's native stereo descriptors.
 * NOTES :
 * ARGS :
 * RETURN : Native table.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const u32* musicBackendTable(void)
{
	if (musicLoader->mode == UYA_MODE_CAMPAIGN)
		return (u32*)0x168648;
#if UYA_MODULE_REGION == 2
	return (u32*)0x1f8408;
#else
	return (u32*)0x1f8588;
#endif
}

/*
 * NAME : musicBackendStream
 * DESCRIPTION : Return only the music stream, leaving speech untouched.
 * NOTES :
 * ARGS :
 * RETURN : Native playback state.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
musicStream_t* musicBackendStream(void)
{
	if (musicLoader->mode == UYA_MODE_CAMPAIGN)
		return (musicStream_t*)(0x1ccfd0 + 0x44);
#if UYA_MODULE_REGION == 2
	return (musicStream_t*)(0x225660 + 0x44);
#else
	return (musicStream_t*)(0x2257e0 + 0x44);
#endif
}

/*
 * NAME : musicBackendReady
 * DESCRIPTION : Check the overlay before using any level-owned music code.
 * NOTES : A loading screen can reuse the previous level ID.
 * ARGS :
 * RETURN : Nonzero when the native entry points and descriptors are ready.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendReady(void)
{
	u32 i;
	int level = musicBackendLevel();
	current = 0;
	for (i = 0; i < sizeof(overlays) / sizeof(overlays[0]); ++i) {
		const musicOverlay_t* entry = &overlays[i];
		if (entry->level != level)
			continue;
		if (musicLoader->mode == UYA_MODE_MULTIPLAYER) {
#if UYA_MODULE_REGION == 2
			if (WORD(0x245848) != 1)
#else
			if (WORD(0x2459c8) != 1)
#endif
				return 0;
		}
		/* A same-map reload can finish between module updates. A restored
		 * retail entry point means our old stream/callbacks no longer own it. */
		if (owner == entry && ownerMode == musicLoader->mode && WORD(entry->play) != JUMP(ignoreTrack))
			return 0;
		if ((owner == entry && ownerMode == musicLoader->mode && WORD(entry->play) == JUMP(ignoreTrack)) ||
		    (WORD(entry->play) == 0x27bdffd0 && WORD(entry->play + 4) == 0x0080582d)) {
			current = entry;
			return musicBackendSector() != 0;
		}
		return 0;
	}
	return 0;
}

/*
 * NAME : ignoreTrack
 * DESCRIPTION : Ignore scripted track requests during manual playback.
 * NOTES : Native music updates and speech continue normally.
 * ARGS :
 * RETURN : Zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int ignoreTrack(void)
{
	return 0;
}

/*
 * NAME : musicBackendControl
 * DESCRIPTION : Keep native deferred handoffs out of manual playlists.
 * NOTES : An already pending retail callback can restore a queued track after acquisition.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicBackendControl(void)
{
	if (owner) {
		musicStream_t* stream = musicBackendStream();
		musicTransition_t* transition = (musicTransition_t*)((u8*)musicBackendStream() - sizeof(musicTransition_t));
		transition->state = 0;
		transition->track = transition->type = -1;
		if (nativeBlocked) {
			stream->handle = 0xffffffff;
			stream->status = 9;
			stream->flags = 0;
			stream->pause = (s16)0x8000;
		}
	}
}

/*
 * NAME : streamStopped
 * DESCRIPTION : Acknowledge the asynchronous stop of our music stream.
 * NOTES :
 * ARGS : Native result and request context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void streamStopped(int result, u64 context)
{
	(void)result;
	if ((u32)(context >> 32) != ownerGeneration)
		return;
	if (owner && ownerMode == musicLoader->mode && ownerLevel == musicBackendLevel() &&
	    (u32)context == (u32)musicBackendStream()) {
		musicBackendStream()->handle = 0;
		musicBackendStream()->status = 0;
	}
	stopping = 0;
}

/*
 * NAME : musicBackendCommand
 * DESCRIPTION : Send a command through the current resident sound client.
 * NOTES : The payload is copied by the native client before returning.
 * ARGS : Command, payload size, payload, completion callback, context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicBackendCommand(int command, int size, void* data, void (*callback)(int, u64), u64 context)
{
	u32 address = musicLoader->mode == UYA_MODE_CAMPAIGN ? 0x13c578 :
#if UYA_MODULE_REGION == 2
	    0x1941c0;
#else
	    0x1942b0;
#endif
	((void (*)(int, int, void*, void (*)(int, u64), u64))address)(command, size, data, callback, context);
}

/*
 * NAME : startupCommand
 * DESCRIPTION : Replace the panoramic native music cue before it becomes audible.
 * NOTES : Only initial music streams are intercepted; speech and effects pass through.
 * ARGS : Native sound-client command and callback.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void startupCommand(int command, int size, void* data, void (*callback)(int, u64), u64 context)
{
	const u32* params = data;
	int level = musicBackendLevel();
#if UYA_MODULE_REGION == 2
	int active = WORD(0x241a50);
#else
	int active = WORD(0x241bd0);
#endif
	if (command == 0x2c && size == 32 && level >= 40 && level <= 49 && !active &&
	    (u32)context == (u32)musicBackendStream() && params[4] == 1 && !params[5]) {
		musicBackendStream()->handle = 0;
		musicBackendStream()->status = 0;
		startupPending = 1;
		startupLevel = level;
		return;
	}
	musicBackendCommand(command, size, data, callback, context);
}

/*
 * NAME : musicBackendStartup
 * DESCRIPTION : Arm a resident cue hook before level loading starts.
 * NOTES : Restore only our own instruction when disabled or leaving multiplayer.
 * ARGS : Nonzero when the next gameplay cue should use a manual playlist.
 * RETURN : Nonzero after intercepting the current level's first music cue.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendStartup(int enabled)
{
#if UYA_MODULE_REGION == 2
	u32 address = 0x194810, original = JAL(0x1941c0);
#else
	u32 address = 0x194900, original = JAL(0x1942b0);
#endif
	if (enabled && musicLoader->mode == UYA_MODE_MULTIPLAYER) {
		if (WORD(address) == original && WORD(address + 4) == 0xafa3001c) {
			WORD(address) = JAL(startupCommand);
			musicLoader->flushCache(0);
			musicLoader->flushCache(2);
		}
	} else {
		if (WORD(address) == JAL(startupCommand)) {
			WORD(address) = original;
			musicLoader->flushCache(0);
			musicLoader->flushCache(2);
		}
		startupPending = 0;
	}
	return startupPending && startupLevel == musicBackendLevel();
}

/*
 * NAME : musicBackendAcquire
 * DESCRIPTION : Take ownership of music requests until release or a map change.
 * NOTES : Native track descriptors stay unchanged.
 * ARGS :
 * RETURN : Nonzero on success.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendAcquire(void)
{
	u32 i, n;
	musicTransition_t* transition;
	if (owner)
		return owner == current && ownerMode == musicLoader->mode;
	if (!current)
		return 0;
	owner = current;
	startupPending = 0;
	++ownerGeneration;
	ownerMode = musicLoader->mode;
	ownerLevel = musicBackendLevel();
	savedTrack = musicBackendStream()->track;
	savedFlags = musicBackendStream()->flags;
	transition = (musicTransition_t*)((u8*)musicBackendStream() - sizeof(musicTransition_t));
	savedQueuedTrack = transition->track;
	savedQueuedType = transition->type;
	/* A previously queued lobby handoff can bypass PlayTrack entirely. */
	transition->state = 0;
	transition->track = transition->type = -1;
	const u32 hooks[] = {owner->play, owner->queue, owner->transition};
	for (i = 0; i < 3; ++i) {
		u32 address = hooks[i];
		if (!address)
			continue;
		for (n = 0; n < 2; ++n)
			originalHooks[i][n] = WORD(address + n * 4);
		WORD(address) = JUMP(ignoreTrack);
		WORD(address + 4) = 0;
	}
	musicLoader->flushCache(0);
	musicLoader->flushCache(2);
	return 1;
}

/*
 * NAME : musicBackendRelease
 * DESCRIPTION : Restore native music requests and release the scheduler guard.
 * NOTES : Never write into an overlay that has already been replaced.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicBackendRelease(void)
{
	u32 i;
	if (owner && ownerMode == musicLoader->mode) {
		musicStream_t* stream = musicBackendStream();
		musicTransition_t* transition = (musicTransition_t*)((u8*)stream - sizeof(musicTransition_t));
		int hooked = ownerLevel == musicBackendLevel() && WORD(owner->play) == JUMP(ignoreTrack);
		/* Resident state survives an overlay replacement. Do not leave our
		 * fake pending handle behind for retail's stream-shutdown wait. */
		if (nativeBlocked && stream->handle == 0xffffffff && stream->status == 9 &&
		    stream->pause == (s16)0x8000 && !stream->flags) {
			stream->handle = 0;
			stream->status = 0;
			stream->pause = 4;
			stream->flags = 0;
			stream->track = -1;
			transition->state = 0;
			transition->track = transition->type = -1;
		}
		if (hooked) {
			if (current == owner) {
				stream->pause = 4;
				stream->flags = savedFlags;
				stream->track = savedTrack;
				transition->state = 0;
				transition->track = savedQueuedTrack;
				transition->type = savedQueuedType;
			}
			const u32 hooks[] = {owner->play, owner->queue, owner->transition};
			for (i = 0; i < 3; ++i) {
				u32 address = hooks[i];
				if (address && WORD(address) == JUMP(ignoreTrack)) {
					WORD(address) = originalHooks[i][0];
					WORD(address + 4) = originalHooks[i][1];
				}
			}
			musicLoader->flushCache(0);
			musicLoader->flushCache(2);
		}
	}
	owner = 0;
	++ownerGeneration;
	stopping = 0;
	nativeBlocked = 0;
}

/*
 * NAME : musicBackendSilence
 * DESCRIPTION : Stop only the native music stream before replacing playback.
 * NOTES : Wait for the game's existing completion callback to clear its handle.
 * ARGS :
 * RETURN : Nonzero when the music stream is idle.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendSilence(void)
{
	musicStream_t* stream = musicBackendStream();
	u32 handle = stream->handle;
	if (nativeBlocked)
		return 1;
	stream->flags = 0;
	/* State 9 suppresses new native stream queries while an outstanding
	 * query returns its handle. Without this, the sync callback only sees
	 * the -1 request sentinel because retail polls again every frame. */
	stream->status = 9;
	if (!handle) {
		nativeBlocked = 1;
		musicBackendControl();
		return 1;
	}
	if (!stopping && handle != 0xffffffff) {
		stopping = 1;
		musicBackendCommand(0x2f, 4, &handle, streamStopped, ((u64)ownerGeneration << 32) | (u32)stream);
	}
	return 0;
}

/*
 * NAME : memcpy
 * DESCRIPTION : Copy module data without binding to one ELF's resident libc.
 * NOTES : Also used by the third-party decoder.
 * ARGS : Destination, source, byte count.
 * RETURN : Destination.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void* memcpy(void* dest, const void* source, u32 size)
{
	u8* out = dest;
	const u8* in = source;
	u32 i;
	for (i = 0; i < size; ++i)
		out[i] = in[i];
	return dest;
}

/*
 * NAME : memmove
 * DESCRIPTION : Move overlapping module buffers in either game ELF.
 * NOTES :
 * ARGS : Destination, source, byte count.
 * RETURN : Destination.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void* memmove(void* dest, const void* source, u32 size)
{
	u8* out = dest;
	const u8* in = source;
	if (out > in) {
		while (size--)
			out[size] = in[size];
	} else {
		memcpy(dest, source, size);
	}
	return dest;
}

/*
 * NAME : memset
 * DESCRIPTION : Clear module state without using a campaign-only libc address.
 * NOTES :
 * ARGS : Destination, value, byte count.
 * RETURN : Destination.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void* memset(void* dest, int value, u32 size)
{
	u8* out = dest;
	u32 i;
	for (i = 0; i < size; ++i)
		out[i] = value;
	return dest;
}

/*
 * NAME : musicBackendPause
 * DESCRIPTION : Pause or resume music while leaving all other audio alone.
 * NOTES : Uses the same pause request bits as retail music_Pause/Unpause.
 * ARGS : Nonzero to pause.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicBackendPause(int paused)
{
	if (owner) {
		musicStream_t* stream = musicBackendStream();
		/* Retail leaves a new stream in state 8 until it queues another
		 * native song. Manual playlists supply their own next track. */
		if (stream->status == 8 && stream->handle && stream->handle != 0xffffffff)
			stream->status = 4;
		stream->pause = paused ? (s16)0x8000 : 4;
	}
}

/*
 * NAME : musicBackendReadWad
 * DESCRIPTION : Read a music-WAD header through the native stream-safe reader.
 * NOTES : One 2048-byte sector; caller supplies a 64-byte aligned buffer.
 * ARGS : Header sector and destination.
 * RETURN : Nonzero on success.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicBackendReadWad(int wad, u32* buffer)
{
	u32 address = musicLoader->mode == UYA_MODE_CAMPAIGN ? 0x13d290 :
#if UYA_MODULE_REGION == 2
	    0x194ed8;
#else
	    0x194fc8;
#endif
	((void (*)(int, int, void*))address)(wad, 1, buffer);
	return buffer[0] == 0x1818 && buffer[1] != 0;
}
