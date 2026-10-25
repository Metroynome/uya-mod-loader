#ifndef UYA_FORGE_H
#define UYA_FORGE_H

#include "module.h"

#define FORGE_EXPORTS_MAGIC (0x31584746)
#define FORGE_SERVICE_ID    (0x464f5247)
#define MAP_HOP_SERVICE_ID  (0x484f5053)
#define FORGE_API_VERSION   (2)

/* Module-owned little-endian EE mailbox. Only request fields belong to the host.
 * Write command/name first, requestId last. One request at a time. */
#define FORGE_MAILBOX_MAGIC   (0x32475246)
#define FORGE_MAILBOX_VERSION (2)
#define FORGE_FILENAME_SIZE   (129)

#ifndef FORGE_MAILBOX
#define FORGE_MAILBOX (&forgeMailbox)
#endif

/* Shared command order: boot, catalog/selection, then game loading controls. */
typedef enum forgeCommand {
	FORGE_LOCAL_PLAY = 1,
	FORGE_REFRESH = 2,
	FORGE_SELECT = 3,
	FORGE_REFRESH_SELECT = 4,
	FORGE_SELECT_RETAIL = 5,
	FORGE_START_GAME = 6,
	FORGE_RELOAD = 7
} forgeCommand_e;

typedef enum forgeContext {
	FORGE_CONTEXT_UNAVAILABLE,
	FORGE_CONTEXT_STAGING,
	FORGE_CONTEXT_GAME,
	FORGE_CONTEXT_BOOT
} forgeContext_e;

typedef enum forgeGameMode {
	FORGE_MODE_SIEGE,
	FORGE_MODE_CTF,
	FORGE_MODE_DM
} forgeGameMode_e;

typedef enum forgeState {
	FORGE_IDLE = 0,
	FORGE_SCANNING = 1,
	FORGE_LOADING = 2,
	FORGE_COMPLETE = 3,
	FORGE_FAILED = 4
} forgeState_e;

typedef enum forgeError {
	FORGE_ERROR_CONTEXT = 1,
	FORGE_ERROR_BUSY = 2,
	FORGE_ERROR_COMMAND = 3,
	FORGE_ERROR_NAME = 4,
	FORGE_ERROR_NOT_FOUND = 5,
	FORGE_ERROR_SCAN = 6,
	FORGE_ERROR_LOAD = 7,
	FORGE_ERROR_INACTIVE = 8,
	FORGE_ERROR_MODE = 9
} forgeError_e;

/* Forge owns the capability; inactive modules must return false. */
/* Offsets below use the EE's 32-bit pointers. */
typedef struct forgeApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*active)(void);
	/* 0x0c */ const int* quickLoadMaps;
	/* 0x10 */ int (*applyGameMode)(void);
} forgeApi_t;

/* Requests execute asynchronously. reload() returns positive when queued;
 * request() returns zero when accepted, or a FORGE_ERROR_* rejection. */
typedef struct mapHopApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*available)(void);
	/* 0x0c */ int (*reload)(void);
	/* 0x10 */ const char* (*status)(void);
	/* 0x14 */ u32 (*context)(void); /* 0 unavailable, 1 staging, 2 game */
	/* 0x18 */ int (*request)(u32 command, const char* filename);
	/* 0x1c */ u32 (*state)(void);
	/* 0x20 */ u32 (*error)(void);
} mapHopApi_t;

typedef struct forgeMailbox {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u16 version;
	/* 0x06 */ u16 size;
	/* 0x08 */ u32 heartbeat;
	/* 0x0c */ u32 requestId;
	/* 0x10 */ u32 acceptedId;
	/* 0x14 */ u32 completedId;
	/* 0x18 */ u32 rejectedId;
	/* 0x1c */ u8 available;
	/* 0x1d */ u8 context;
	/* 0x1e */ u8 capabilities;
	/* 0x1f */ u8 command;
	/* 0x20 */ u8 state;
	/* 0x21 */ u8 error;
	/* 0x22 */ u8 rejectError;
	/* 0x23 */ u8 gameMode; /* Request: 0 Siege (default), 1 CTF, 2 Deathmatch. */
	/* 0x24 */ char filename[FORGE_FILENAME_SIZE];
	/* 0xa5 */ u8 padding[3];
} forgeMailbox_t;

/* Offsets are for 32-bit EE pointers. Export discovery remains version 1. */
typedef struct forgeExports {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 version;
	/* 0x08 */ u32 size;
	/* 0x0c */ volatile forgeMailbox_t* mailbox;
} forgeExports_t;

typedef char ForgeMailboxSize[(sizeof(forgeMailbox_t) == 168) ? 1 : -1];

extern volatile forgeMailbox_t forgeMailbox;
#endif
