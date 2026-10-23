#ifndef UYA_HOST_MAPLOADER_H
#define UYA_HOST_MAPLOADER_H

#include "module.h"
#include "include/forge.h"

#define MAP_LIMIT 96

typedef struct MapRecord {
	char filename[FORGE_FILENAME_SIZE], name[32];
	u32 version, base, wad, world, sound, radar;
} MapRecord_t;

typedef char MapRecordSize[(sizeof(MapRecord_t) == 188) ? 1 : -1];

extern const ModuleApi_t* mapApi;
extern MapRecord_t* mapRecords;
extern int mapCount, mapSelection;
extern int mapCatalogReady;
extern u32 mapCatalogRevision;
extern int mapAllowOnline;
extern int mapHopSawLoading, mapHopResult;
extern u32 mapSceneRequest;

void mapCopyRecord(MapRecord_t* to, const MapRecord_t* from);
int mapReadInfo(const MapRecord_t* map, char* author, char* description);
int mapReadExact(int fd, void* dest, u32 size);
int mapOpenPart(const MapRecord_t* map, const char* extension);
int mapReadPart(const MapRecord_t* map, const char* extension, void* dest, u32 size);
void* memset(void* dest, int value, u32 size);
void* mapAlloc(u32 size);
void mapFree(void* p);
int mapDirectoryIdle(void);
void mapDirectoryReset(void);
int mapDirectoryReady(void);
int mapDirectoryRequest(int command);
int mapDirectoryPoll(char* name);
void mapScanStart(void);
int mapScanTick(void);
void mapScanRelease(void);
int mapScanActive(void);
int mapScanCurrent(void);
int mapScanTotal(void);
int mapValidate(const MapRecord_t* map);
int mapOnlineDisabled(void);
int mapMultiplayerReady(void);
/* 0 = unavailable, 1 = local staging, 2 = online staging. */
int mapStaging(void);
void mapUpdateName(void);
int mapChooseBase(u32 base);
int mapChooseRetail(u32 base);
u32 mapCurrentBase(void);
const char* mapRetailName(u32 base);
int mapCompareText(const char* a, const char* b);
void mapRestoreBase(void);
int mapHooksReady(void);
void mapInstallHooks(void);
void mapDisableHooks(void);
int mapLoading(void);
void mapClearLoad(void);
void mapMessage(const char* message);
int mapForgeActive(void);
int mapHopInGame(void);
int mapHopAvailable(void);
/* Nonnegative values are catalog indices; -40 through -49 select retail bases. */
int mapHopRequest(int index);
int mapStartGame(void);
void mapHopTick(void);
void mapHopCancel(void);
void mapHopOnLoad(void);
#endif
