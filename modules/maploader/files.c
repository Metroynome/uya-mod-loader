#include "maploader.h"

#ifndef MAP_SEEK
#define MAP_SEEK ((int (*)(int, int, int))0x12ae40)
#endif

/* Keep repeated catalog moves out of the scan state machine. */
void mapCopyRecord(MapRecord_t* to, const MapRecord_t* from) __attribute__((noinline));

const ModuleApi_t* mapApi;
MapRecord_t* mapRecords;
int mapCount, mapSelection = -1;
int mapCatalogReady;
/* Scan state is driven one entry/RPC at a time from Local Play staging. */
/* One retained allocation, refilled in place on explicit refresh. */
static char selected[FORGE_FILENAME_SIZE];
static int phase, candidates, current, kept, waitTicks;
u32 mapCatalogRevision;

static int validName(const char* text, u32 size)
{
	u32 i;
	if (!text[0] || text[0] == '.')
		return 0;
	for (i = 0; i < size; ++i) {
		u8 c = text[i];
		if (!c)
			return 1;
		if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':' || c == '"' || c == '*' || c == '?')
			return 0;
	}
	return 0;
}

static void append(char* dst, u32* pos, const char* src)
{
	while (*src)
		dst[(*pos)++] = *src++;
	dst[*pos] = 0;
}

int mapReadExact(int fd, void* dest, u32 size)
{
	u8* p = dest;
	while (size) {
		u32 chunk = size > 0x10000 ? 0x10000 : size;
		int n = mapApi->read(fd, p, chunk);
		if (n <= 0 || (u32)n > chunk)
			return 0;
		p += n;
		size -= n;
	}
	return 1;
}

int mapOpenPart(const MapRecord_t* map, const char* extension)
{
	char path[176];
	u32 pos = 0;
	if (!validName(map->filename, sizeof(map->filename)))
		return -1;
	append(path, &pos, "host0:uya/");
	append(path, &pos, map->filename);
#if UYA_MODULE_REGION == 2
	append(path, &pos, ".pal");
#endif
	append(path, &pos, extension);
	return mapApi->open(path, 1, 0);
}

int mapReadPart(const MapRecord_t* map, const char* extension, void* dest, u32 size)
{
	int fd = mapOpenPart(map, extension), ok;
	u8 tail;
	if (fd < 0)
		return 0;
	ok = mapReadExact(fd, dest, size) && mapApi->read(fd, &tail, 1) == 0;
	mapApi->close(fd);
	return ok;
}

/* MP's resident seek has been checked in both regional SDK images. Campaign
 * catalogs can be read via ModuleApi_t, but no campaign game-code hooks are used. */
static int sizeMatches(const MapRecord_t* map, const char* extension, u32 size)
{
	int fd, result;
	if (!size)
		return 1;
	fd = mapOpenPart(map, extension);
	if (fd < 0)
		return 0;
	result = MAP_SEEK(fd, 0, 2);
	mapApi->close(fd);
	return result == (int)size;
}

int mapValidate(const MapRecord_t* map)
{
	if (!mapMultiplayerReady())
		return 0;
	return sizeMatches(map, ".wad", map->wad) && sizeMatches(map, ".world", map->world) &&
	       sizeMatches(map, ".sound", map->sound) && sizeMatches(map, ".map", map->radar);
}

void* memset(void* dest, int value, u32 size)
{
	u8* p = dest;
	while (size--)
		*p++ = (u8)value;
	return dest;
}

/* .version metadata is shared by both regions: 48-byte header, author[32],
 * description[256]. Older packs may contain just the header. */
int mapReadInfo(const MapRecord_t* map, char* author, char* description)
{
	char path[176], header[48];
	u32 pos = 0, i;
	int fd, ok;
	author[0] = description[0] = 0;
	if (!validName(map->filename, sizeof(map->filename)))
		return 0;
	append(path, &pos, "host0:uya/");
	append(path, &pos, map->filename);
	append(path, &pos, ".version");
	fd = mapApi->open(path, 1, 0);
	if (fd < 0)
		return 0;
	ok = mapReadExact(fd, header, 48) && mapReadExact(fd, author, 32) && mapReadExact(fd, description, 256);
	mapApi->close(fd);
	if (!ok) {
		author[0] = description[0] = 0;
		return 0;
	}
	author[31] = 0;
	description[255] = 0;
	for (i = 0; author[i]; ++i)
		if ((u8)author[i] < 32)
			author[i] = ' ';
	for (i = 0; description[i]; ++i)
		if ((u8)description[i] < 32 && description[i] != '\n')
			description[i] = ' ';
	return 1;
}

int mapCompareText(const char* a, const char* b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return (u8)*a - (u8)*b;
}

static void copyName(char* to, const char* from)
{
	while ((*to++ = *from++))
		;
}

static int validRecord(const MapRecord_t* m)
{
	return validName(m->filename, sizeof(m->filename)) && validName(m->name, 32) && m->base >= 40 && m->base <= 49 &&
	       (m->wad || m->world) && m->wad <= 0x1000000 && m->world <= 0x400000 && m->sound <= 0x400000 &&
	       m->radar <= 0x10800;
}

static int partSize(const MapRecord_t* m, const char* ext)
{
	int fd = mapOpenPart(m, ext), size;
	if (fd < 0)
		return 0;
	size = MAP_SEEK(fd, 0, 2);
	if (mapApi->close(fd) < 0)
		return -1;
	return size;
}

static int inspect(MapRecord_t* m)
{
	u8 data[384] __attribute__((aligned(64)));
	char path[176];
	u32 pos = 0;
	int fd, n, total = 0, code;
	append(path, &pos, "host0:uya/");
	append(path, &pos, m->filename);
	append(path, &pos, ".version");
	fd = mapApi->open(path, 1, 0);
	if (fd < 0)
		return 0;
	memset(data, 0, sizeof(data));
	while (total < (int)sizeof(data)) {
		n = mapApi->read(fd, data + total, sizeof(data) - total);
		if (n < 0 || n > (int)sizeof(data) - total) {
			mapApi->close(fd);
			return -1;
		}
		if (!n)
			break;
		total += n;
	}
	if (mapApi->close(fd) < 0)
		return -1;
	if (total < 48 || *(int*)data < 0 || *(short*)(data + 8))
		return 0;
	m->version = *(u32*)data;
	m->base = *(u32*)(data + 4);
	for (n = 0; n < 32 && data[16 + n]; ++n)
		m->name[n] = data[16 + n];
	code = mapOpenPart(m, ".code");
	if (code >= 0) {
		mapApi->close(code);
		return 0;
	}
	n = partSize(m, ".wad");
	if (n < 0)
		return -1;
	m->wad = n;
	n = partSize(m, ".world");
	if (n < 0)
		return -1;
	m->world = n;
	n = partSize(m, ".sound");
	if (n < 0)
		return -1;
	m->sound = n;
	n = partSize(m, ".map");
	if (n < 0)
		return -1;
	m->radar = n;
	if (!validRecord(m))
		return 0;
	return 1;
}

static void finish(int error)
{
	phase = 0;
	if (error) {
		mapCount = 0;
		mapCatalogReady = 0;
		if (selected[0]) {
			mapRestoreBase();
			mapClearLoad();
		}
		selected[0] = 0;
		mapMessage("Map scan failed; use Refresh Custom Maps to retry");
	} else
		mapCatalogReady = 1;
}

void mapScanStart(void)
{
	if (phase || !mapMultiplayerReady())
		return;
	if (!mapDirectoryIdle()) {
		mapMessage("Directory service is still busy");
		return;
	}
	if (!mapRecords)
		mapRecords = mapAlloc(MAP_LIMIT * sizeof(MapRecord_t));
	if (!mapRecords) {
		mapMessage("Not enough memory to scan custom maps");
		return;
	}
	selected[0] = 0;
	if (mapSelection >= 0 && mapSelection < mapCount)
		copyName(selected, mapRecords[mapSelection].filename);
	mapCount = 0;
	mapSelection = -1;
	mapCatalogReady = 0;
	memset(mapRecords, 0, MAP_LIMIT * sizeof(MapRecord_t));
	candidates = current = kept = waitTicks = 0;
	++mapCatalogRevision; /* Re-read descriptions even when the catalog is unchanged. */
	mapDirectoryReset();
	phase = 1;
}

int mapScanActive(void)
{
	return phase != 0;
}

int mapScanCurrent(void)
{
	return current;
}

int mapScanTotal(void)
{
	return phase == 4 ? candidates : 0;
}

void mapCopyRecord(MapRecord_t* to, const MapRecord_t* from)
{
	u32 i;
	u8* d = (u8*)to;
	const u8* s = (const u8*)from;
	for (i = 0; i < sizeof(*to); ++i)
		d[i] = s[i];
}

int mapScanTick(void)
{
	int r, n, i;
	char name[256];
	MapRecord_t temp;
	if (!phase)
		return 0;
	if (++waitTicks > 6000) {
		finish(1);
		return -1;
	}
	if (phase == 1) {
		r = mapDirectoryReady();
		if (r < 0) {
			finish(1);
			return -1;
		}
		if (r > 0) {
			if (!mapDirectoryRequest(1)) {
				finish(1);
				return -1;
			}
			phase = 2;
			waitTicks = 0;
		}
		return 0;
	}
	if (phase == 2 || phase == 3 || phase == 5) {
		r = mapDirectoryPoll(name);
		if (r == -999)
			return 0;
		waitTicks = 0;
		if (phase == 5) {
			if (r < 0) {
				finish(1);
				return -1;
			}
			phase = 4;
			return 0;
		}
		if (r < 0) {
			mapDirectoryRequest(3);
			finish(1);
			return -1;
		}
		if (phase == 3 && !r) {
			if (!mapDirectoryRequest(3)) {
				finish(1);
				return -1;
			}
			phase = 5;
			return 0;
		}
		if (phase == 3) {
			for (n = 0; name[n]; ++n)
				;
			if (n > 8 && n < (int)FORGE_FILENAME_SIZE + 8 && !mapCompareText(name + n - 8, ".version")) {
				name[n - 8] = 0;
				if (validName(name, FORGE_FILENAME_SIZE)) {
					if (candidates == MAP_LIMIT) {
						mapDirectoryRequest(3);
						finish(1);
						return -1;
					}
					copyName(mapRecords[candidates++].filename, name);
				}
			}
		}
		if (!mapDirectoryRequest(2)) {
			finish(1);
			return -1;
		}
		phase = 3;
		return 0;
	}
	if (phase == 4 && current < candidates) {
		r = inspect(&mapRecords[current]);
		if (r < 0) {
			finish(1);
			return -1;
		}
		if (r)
			mapCopyRecord(&mapRecords[kept++], &mapRecords[current]);
		++current;
		waitTicks = 0;
		return 0;
	}
	/* Sort the completed catalog by display name. */
	for (n = 1; n < kept; ++n) {
		mapCopyRecord(&temp, &mapRecords[n]);
		i = n;
		while (i && mapCompareText(mapRecords[i - 1].name, temp.name) > 0) {
			mapCopyRecord(&mapRecords[i], &mapRecords[i - 1]);
			--i;
		}
		mapCopyRecord(&mapRecords[i], &temp);
	}
	mapCount = kept;
	for (i = 0; i < mapCount; ++i)
		if (!mapCompareText(selected, mapRecords[i].filename))
			mapSelection = i;
	if (selected[0] && mapSelection < 0) {
		mapRestoreBase();
		mapClearLoad();
	}
	++mapCatalogRevision;
	selected[0] = 0;
	mapMessage("Custom maps refreshed");
	finish(0);
	return 1;
}

void mapScanRelease(void)
{
	/* Never free an old multiplayer pointer through the campaign allocator. */
	if (phase && mapApi->mode == UYA_MODE_MULTIPLAYER && mapDirectoryIdle())
		mapDirectoryRequest(3);
	phase = 0;
	selected[0] = 0;
	mapFree(mapRecords);
	mapRecords = 0;
	mapCount = 0;
	mapSelection = -1;
	mapCatalogReady = 0;
}
