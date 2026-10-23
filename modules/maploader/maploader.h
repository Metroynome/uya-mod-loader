#ifndef UYA_HOST_MAPLOADER_H
#define UYA_HOST_MAPLOADER_H

#include "module.h"

#define MAP_LIMIT 96

typedef struct MapRecord {
	char filename[64], name[32];
	unsigned int version, base, wad, world, sound, radar;
} MapRecord;

typedef char MapRecordSize[(sizeof(MapRecord) == 120) ? 1 : -1];

extern const ModuleApi* mapApi;
extern MapRecord* mapRecords;
extern int mapCount, mapSelection;
extern int mapCatalogReady;
extern unsigned int mapCatalogRevision;
extern int mapAllowOnline;
extern int mapHopSawLoading, mapHopResult;
extern unsigned int mapSceneRequest;

void mapCopyRecord(MapRecord* to, const MapRecord* from);
int mapReadInfo(const MapRecord* map, char* author, char* description);
int mapReadExact(int fd, void* dest, unsigned int size);
int mapOpenPart(const MapRecord* map, const char* extension);
int mapReadPart(const MapRecord* map, const char* extension, void* dest, unsigned int size);
void* memset(void* dest, int value, unsigned int size);
void* mapAlloc(unsigned int size);
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
int mapValidate(const MapRecord* map);
int mapOnlineDisabled(void);
int mapMultiplayerReady(void);
/* 0 = unavailable, 1 = local staging, 2 = online staging. */
int mapStaging(void);
void mapUpdateName(void);
int mapChooseBase(unsigned int base);
int mapChooseRetail(unsigned int base);
unsigned int mapCurrentBase(void);
const char* mapRetailName(unsigned int base);
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
