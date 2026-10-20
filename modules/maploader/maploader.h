#ifndef UYA_HOST_MAPLOADER_H
#define UYA_HOST_MAPLOADER_H
#include "module.h"
#define MAP_LIMIT 96
#define MAP_INDEX_MAGIC 0x3150414dU
/* On-disk little-endian catalog record. Keep in sync with index_maps.py. */
typedef struct MapRecord {
    char filename[64], name[32];
    unsigned int version, base, wad, world, sound, radar;
} MapRecord;
typedef char MapRecordSize[(sizeof(MapRecord)==120)?1:-1];
extern const ModuleApi *mapApi;
extern MapRecord mapRecords[MAP_LIMIT];
extern int mapCount, mapSelection;
int mapReadInfo(const MapRecord *map,char *author,char *description);
int mapReadExact(int fd, void *dest, unsigned int size);
int mapOpenPart(const MapRecord *map, const char *extension);
int mapReadPart(const MapRecord *map, const char *extension, void *dest, unsigned int size);
int mapLoadCatalog(void);
int mapValidate(const MapRecord *map);
extern int mapAllowOnline;
int mapOnlineDisabled(void);
int mapMultiplayerReady(void);
/* 0 = unavailable, 1 = local staging, 2 = online staging. */
int mapStaging(void);
void mapUpdateName(void);
int mapChooseBase(unsigned int base);
void mapRestoreBase(void);
int mapHooksReady(void);
void mapInstallHooks(void);
int mapLoading(void);
void mapClearLoad(void);
void mapMessage(const char *message);
#endif
