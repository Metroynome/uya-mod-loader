#ifndef UYA_FORGE_H
#define UYA_FORGE_H

#include "module.h"

#define FORGE_EXPORTS_MAGIC 0x31584746U
#define FORGE_SERVICE_ID    0x464f5247U
#define MAP_HOP_SERVICE_ID  0x484f5053U
#define FORGE_API_VERSION   2U

/* Module-owned little-endian EE mailbox. Only request fields belong to the host.
 * Write command/name first, requestId last. One request at a time. */
#define FORGE_MAILBOX_MAGIC   0x32475246U
#define FORGE_MAILBOX_VERSION 2U
#define FORGE_FILENAME_SIZE   129U
#define FORGE_RELOAD          1U
#define FORGE_REFRESH         2U
#define FORGE_SELECT          3U
#define FORGE_REFRESH_SELECT  4U
#define FORGE_START_GAME      5U
#define FORGE_SELECT_RETAIL   6U
#define FORGE_IDLE            0U
#define FORGE_SCANNING        1U
#define FORGE_LOADING         2U
#define FORGE_COMPLETE        3U
#define FORGE_FAILED          4U
#define FORGE_ERROR_CONTEXT   1U
#define FORGE_ERROR_BUSY      2U
#define FORGE_ERROR_COMMAND   3U
#define FORGE_ERROR_NAME      4U
#define FORGE_ERROR_NOT_FOUND 5U
#define FORGE_ERROR_SCAN      6U
#define FORGE_ERROR_LOAD      7U
#define FORGE_ERROR_INACTIVE  8U

#ifndef FORGE_MAILBOX
#define FORGE_MAILBOX (&forgeMailbox)
#endif

/* Forge owns the capability; inactive modules must return false. */
/* Offsets below use the EE's 32-bit pointers. */
typedef struct ForgeApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*active)(void);
} ForgeApi_t;

/* Requests execute asynchronously. reload() returns positive when queued;
 * request() returns zero when accepted, or a FORGE_ERROR_* rejection. */
typedef struct MapHopApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*available)(void);
	/* 0x0c */ int (*reload)(void);
	/* 0x10 */ const char* (*status)(void);
	/* 0x14 */ u32 (*context)(void); /* 0 unavailable, 1 staging, 2 game */
	/* 0x18 */ int (*request)(u32 command, const char* filename);
	/* 0x1c */ u32 (*state)(void);
	/* 0x20 */ u32 (*error)(void);
} MapHopApi_t;

typedef struct ForgeMailbox {
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
	/* 0x23 */ u8 reserved;
	/* 0x24 */ char filename[FORGE_FILENAME_SIZE];
	/* 0xa5 */ u8 padding[3];
} ForgeMailbox_t;

/* Offsets are for 32-bit EE pointers. Export discovery remains version 1. */
typedef struct ForgeExports {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 version;
	/* 0x08 */ u32 size;
	/* 0x0c */ volatile ForgeMailbox_t* mailbox;
} ForgeExports_t;

typedef char ForgeMailboxSize[(sizeof(ForgeMailbox_t) == 168) ? 1 : -1];

extern volatile ForgeMailbox_t forgeMailbox;
#endif
