#ifndef UYA_FORGE_H
#define UYA_FORGE_H
#include "module.h"
#define FORGE_SERVICE_ID 0x464f5247U
#define MAP_HOP_SERVICE_ID 0x484f5053U
#define FORGE_API_VERSION 1U
/* Forge owns the capability; inactive modules must return false. */
typedef struct ForgeApi { unsigned int version,size; int (*active)(void); } ForgeApi;
/* Requests are accepted asynchronously. A positive return means queued, not loaded. */
typedef struct MapHopApi {
    unsigned int version,size;
    int (*available)(void);
    int (*reload)(void);
    const char *(*status)(void);
} MapHopApi;
#endif
