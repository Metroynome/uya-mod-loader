#ifndef UYA_MUSICPLAYER_H
#define UYA_MUSICPLAYER_H

#include "module.h"
#include <librac3/music.h>

#define MUSIC_MAX_TRACKS (160)
#define MUSIC_CUSTOM_PATH ("host0:uya/music/")

typedef enum musicPlaylist {
	MUSIC_BASE,
	MUSIC_GAME,
	MUSIC_CUSTOM,
	MUSIC_GAME_SHUFFLE,
	MUSIC_CUSTOM_SHUFFLE,
	MUSIC_COMBINED,
	MUSIC_COMBINED_SHUFFLE
} musicPlaylist_e;

typedef enum musicTrackPart {
	MUSIC_STANDALONE,
	MUSIC_INTRO,
	MUSIC_BODY
} musicTrackPart_e;

/* Native EE stereo descriptors: sector offset and length for each channel. */
typedef struct musicTrack {
	/* 0x00 */ u32 channels[4];
	/* 0x10 */ char name[64];
	/* 0x50 */ musicTrackPart_e part;
} musicTrack_t;

extern const moduleApi_t* musicLoader;

int musicBackendReady(void);
int musicBackendLevel(void);
int musicBackendContext(void);
int musicBackendStartup(int enabled);
u32 musicBackendSector(void);
const u32* musicBackendTable(void);
musicStream_t* musicBackendStream(void);
int musicBackendAcquire(void);
void musicBackendControl(void);
int musicBackendSilence(void);
void musicBackendRelease(int restore);
void musicBackendPause(int paused);
int musicBackendReadWad(int wad, u32* buffer);
void musicBackendCommand(int command, int size, void* data, void (*callback)(int, u64), u64 context);
void musicText(char* output, const char* name, int number, const char* suffix);
void musicCustomRefresh(void);
int musicCustomScan(void);
int musicCustomCount(void);
const char* musicCustomName(int index);
int musicCustomOpen(int index);
int musicDiscOpen(const musicTrack_t* track, const musicTrack_t* body, int loop);
int musicCustomTick(void);
void musicCustomPause(int paused);
void musicCustomClose(void);

#endif
