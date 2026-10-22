#ifndef UYA_FORGE_H
#define UYA_FORGE_H
#include "module.h"
#define FORGE_SERVICE_ID 0x464f5247U
#define MAP_HOP_SERVICE_ID 0x484f5053U
#define FORGE_API_VERSION 1U
/* Forge owns the capability; inactive modules must return false. */
typedef struct ForgeApi { unsigned int version,size; int (*active)(void); } ForgeApi;
/* Requests execute asynchronously. reload() returns positive when queued;
 * request() returns zero when accepted, or a FORGE_ERROR_* rejection. */
typedef struct MapHopApi {
    unsigned int version,size;
    int (*available)(void);
    int (*reload)(void);
    const char *(*status)(void);
    unsigned int (*context)(void); /* 0 unavailable, 1 staging, 2 match */
    int (*request)(unsigned int command,const char *filename);
    unsigned int (*state)(void);
    unsigned int (*error)(void);
} MapHopApi;
/* Module-owned little-endian EE mailbox. Only request fields belong to the host.
 * Write command/name first, requestId last. One request at a time. */
#define FORGE_MAILBOX_MAGIC 0x31475246U
#define FORGE_RELOAD 1U
#define FORGE_REFRESH 2U
#define FORGE_SELECT 3U
#define FORGE_REFRESH_SELECT 4U
#define FORGE_START_GAME 5U
#define FORGE_SELECT_RETAIL 6U
#define FORGE_IDLE 0U
#define FORGE_SCANNING 1U
#define FORGE_LOADING 2U
#define FORGE_COMPLETE 3U
#define FORGE_FAILED 4U
#define FORGE_ERROR_CONTEXT 1U
#define FORGE_ERROR_BUSY 2U
#define FORGE_ERROR_COMMAND 3U
#define FORGE_ERROR_NAME 4U
#define FORGE_ERROR_NOT_FOUND 5U
#define FORGE_ERROR_SCAN 6U
#define FORGE_ERROR_LOAD 7U
#define FORGE_ERROR_INACTIVE 8U
typedef struct ForgeMailbox {
    unsigned int magic,version,size,available,context,capabilities,heartbeat;
    unsigned int requestId,command;
    char filename[64];
    unsigned int acceptedId,completedId,state,error,rejectedId,rejectError;
    char message[48];
} ForgeMailbox;
#ifndef FORGE_MAILBOX
extern volatile ForgeMailbox forgeMailbox;
#define FORGE_MAILBOX (&forgeMailbox)
#endif
typedef char ForgeMailboxFits[(sizeof(ForgeMailbox)<=256)?1:-1];
#endif
