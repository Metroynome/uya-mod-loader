/* UYA host module loader. No heap resizing or game allocator patches.
 * Flow: choose the current game's API -> read modules.txt once -> validate and
 * load each image -> initialize/update only modules matching the active mode.
 * Memory: discovery at a0000; code, table and state follow. Modules start at
 * the aligned loader end, or at 02000000 when expanded EE RAM is available.
 */
#include "module.h"
#include "config.h"
#include "exception.h"
#include "platform.h"
#include "strings.h"

#ifndef MODULE_START
#define MODULE_START (((u32)_loader_end + 63U) & ~63U)
#endif
#define MODULE_END      0x00100000U
#define EXTRA_RAM_START 0x02000000U
#define EXTRA_RAM_END   0x08000000U
#define MODULE_LIMIT    UYA_MODULE_LIMIT
#define LOADER_MAGIC    0x554d4c40U
#ifndef STATE
#define STATE (&loaderState)
#endif
#ifndef MODULE_TABLE
#define MODULE_TABLE (&moduleTable)
#endif
#define loaderApi (STATE->api)
#define printf    loaderApi.print
#define fileOpen  loaderApi.open
#define fileRead  loaderApi.read
#define fileClose loaderApi.close
/* Named list values carry a tag so legacy numeric modes retain their ABI. */
#define NAMED_CONTEXTS 0x100U

typedef struct LoadedModule {
	u32 address, size;
	ModuleUpdate update;
	ModuleInit init;
	u32 modes, initialized, contexts, policy, active;
	ModuleLifecycle activate, deactivate;
} LoadedModule_t;

typedef struct ModuleService {
	u32 id, version, owner;
	const void* table;
} ModuleService_t;

typedef struct LoaderState {
	u32 magic, busy, count, mode, listLoaded, retryTicks, exceptionAttempted, exceptionReserved;
	LoadedModule_t modules[MODULE_LIMIT];
	ModuleApi_t api;
	int configBusy;
	u32 currentModule; /* index + 1; zero outside callbacks */
	ModuleService_t services[8];
	u32 poolStart, poolEnd;
} LoaderState_t;

extern u8 _loader_end[];
extern void* GetSyscallHandler(int syscall);

#ifdef _EE
/* Linker-placed after the payload, never overwritten by recurring PNACH writes. */
ModuleTable_t moduleTable __attribute__((section(".loader.state")));
static LoaderState_t loaderState __attribute__((section(".loader.state")));
#endif

const ModuleApi_t* moduleConfigApi(void)
{
	return &STATE->api;
}

/* Choose once per boot. Never probe unavailable RAM or move live modules.
 * PCSX2 reports expanded RAM through the normal GetMemorySize syscall.
 */
static void selectModulePool(u32 memorySize)
{
	STATE->poolStart = MODULE_START;
	STATE->poolEnd = MODULE_END;
	if (memorySize == EXTRA_RAM_END) {
		/* Inspect the handler, never invoke an unsupported BIOS syscall. */
		if (GetSyscallHandler(0x82)) {
			STATE->poolStart = EXTRA_RAM_START;
			STATE->poolEnd = EXTRA_RAM_END;
		} else {
			loaderApi.warnings |= UYA_WARNING_EXTRA_RAM_BIOS;
		}
	}
}

int* moduleConfigBusy(void)
{
	return &STATE->configBusy;
}

static u32 modeContexts(u32 mode)
{
	return (mode & 1 ? UYA_CONTEXT_CAMPAIGN : 0) | (mode & 2 ? (UYA_CONTEXT_LOCAL | UYA_CONTEXT_ONLINE) : 0);
}

static int moduleAllowed(const LoadedModule_t* m)
{
	return (m->modes & STATE->mode) && (m->contexts & m->policy & loaderApi.context);
}

static int setLifecycle(ModuleLifecycle activate, ModuleLifecycle deactivate)
{
	LoadedModule_t* m;
	if (!STATE->currentModule)
		return -1;
	m = &STATE->modules[STATE->currentModule - 1];
	m->activate = activate;
	m->deactivate = deactivate;
	return 0;
}

static int restrictContexts(u32 contexts)
{
	if (!STATE->currentModule || (contexts & ~UYA_CONTEXT_ALL))
		return -1;
	STATE->modules[STATE->currentModule - 1].policy = contexts;
	return 0;
}

/* Modules can export APIs without hard-coded addresses or load-order coupling.
 * Services are visible only while their owner's mode is active. */
static int publishService(u32 id, u32 version, const void* table)
{
	u32 i;
	if (!id || !version || !table || !STATE->currentModule)
		return -1;
	for (i = 0; i < 8; ++i) {
		ModuleService_t* s = &STATE->services[i];
		if (s->id == id) {
			if (s->owner != STATE->currentModule || s->version != version || s->table != table)
				return -1;
			return 0; /* Idempotent retry by the same publisher. */
		}
	}
	for (i = 0; i < 8; ++i) {
		ModuleService_t* s = &STATE->services[i];
		if (!s->id) {
			s->id = id;
			s->version = version;
			s->owner = STATE->currentModule;
			s->table = table;
			return 0;
		}
	}
	return -1;
}

static const void* findService(u32 id, u32 version)
{
	u32 i;
	for (i = 0; i < 8; ++i) {
		ModuleService_t* s = &STATE->services[i];
		if (s->id == id && s->version == version && s->owner && moduleAllowed(&STATE->modules[s->owner - 1]))
			return s->table;
	}
	return 0;
}

static int readExact(int fd, void* buffer, u32 size)
{
	u32 done = 0;
	while (done < size) {
		int count = fileRead(fd, (char*)buffer + done, size - done);
		if (count <= 0 || (u32)count > size - done)
			return 0;
		done += count;
	}
	return 1;
}

static int atEnd(int fd)
{
	char extra;
	return fileRead(fd, &extra, 1) == 0;
}

static void syncCode(void)
{
	loaderApi.flushCache(0);
	loaderApi.flushCache(2);
}

static void installExceptionHandler(void)
{
	u8* memory = (u8*)EXCEPTION_ADDRESS;
	u32 i, hash = 2166136261U;
	int fd, valid;
	u32 address;

	/* Optional and attempted once per boot, before any module initialization.
     * Missing/invalid payloads never prevent normal module loading. */
	if (STATE->exceptionAttempted)
		return;
	STATE->exceptionAttempted = 1;
	if (UYA_LOADER_DISCOVERY_ADDRESS < EXCEPTION_ADDRESS + EXCEPTION_MEMORY_SIZE && MODULE_START > EXCEPTION_ADDRESS)
		return; /* The loader itself must never overlap the fixed handler. */
	fd = fileOpen("host0:uya/mods/debug/exceptionhandler.bin", 1, 0);
	if (fd < 0) {
		printf("loader: optional exception handler unavailable (%d)\n", fd);
		return;
	}
	for (i = 0; i < EXCEPTION_MEMORY_SIZE; ++i) {
		if (memory[i]) {
			STATE->exceptionReserved = 1; /* Preserve existing code/workspace too. */
			printf("loader: exception handler area is occupied\n");
			fileClose(fd);
			return;
		}
	}
	valid = readExact(fd, memory, EXCEPTION_FILE_SIZE) && atEnd(fd);
	if (fileClose(fd) < 0)
		valid = 0;
	for (i = 0; valid && i < EXCEPTION_FILE_SIZE; ++i)
		hash = (hash ^ memory[i]) * 16777619U;
	if (!valid || hash != EXCEPTION_HASH) {
		memset(memory, 0, EXCEPTION_MEMORY_SIZE);
		printf("loader: rejected exception handler (size/read/hash)\n");
		return;
	}
	syncCode();
	printf("loader: installing exception handler at %08x\n", EXCEPTION_ADDRESS);
	STATE->exceptionReserved = 1;
	EXCEPTION_INSTALL();
	syncCode();
	printf("loader: exception handler installed\n");
}

static int validPath(const char* path)
{
	const char* start = path;
	if (!*path)
		return 0;
	for (;;) {
		if (*path == ':' || *path == '\\')
			return 0;
		if (!*path || *path == '/') {
			int length = path - start;
			if (!length || (length == 1 && start[0] == '.') || (length == 2 && start[0] == '.' && start[1] == '.'))
				return 0;
			if (!*path)
				return 1;
			start = path + 1;
		}
		++path;
	}
}

static int validEntry(u32 offset, u32 size)
{
	return offset == UYA_MODULE_NO_ENTRY || (!(offset & 3) && offset >= sizeof(ModuleDiscovery_t) && offset < size);
}

/* A small monotonic allocator: successful loads retain their slots until restart.
 * Failed loads never consume space. No game heap edits or per-module addresses. */
static u32 nextModuleAddress(u32 size)
{
	u32 address = STATE->poolStart;
	if (STATE->count)
		address = (STATE->modules[STATE->count - 1].address + STATE->modules[STATE->count - 1].size + 63U) & ~63U;
	/* Keep each module contiguous. A module crossing the handler starts after
	 * its workspace; absent/rejected handlers leave the whole pool available. */
	if (STATE->exceptionReserved && address < EXCEPTION_ADDRESS + EXCEPTION_MEMORY_SIZE &&
	    (address >= EXCEPTION_ADDRESS || size > EXCEPTION_ADDRESS - address))
		address = EXCEPTION_ADDRESS + EXCEPTION_MEMORY_SIZE;
	return address;
}

static int validHeader(const ModuleHeader_t* h)
{
	u32 address = nextModuleAddress(h->memorySize);
	return h->magic == UYA_MODULE_MAGIC && h->version == UYA_MODULE_VERSION &&
	       (!h->region || h->region == UYA_MODULE_REGION) && h->address == 0x000f0000 && h->modes &&
	       !(h->modes & ~3U) && h->imageSize >= sizeof(ModuleDiscovery_t) && !(h->imageSize & 3) &&
	       h->imageSize <= h->memorySize && address < STATE->poolEnd && h->memorySize <= STATE->poolEnd - address &&
	       h->relocationCount <= (h->imageSize - sizeof(ModuleDiscovery_t)) / 4 &&
	       validEntry(h->initOffset, h->imageSize) && validEntry(h->updateOffset, h->imageSize);
}

/* The build preserves only references into the module. Game addresses stay
 * absolute. Read records incrementally so relocation metadata needs no heap. */
static int relocateModule(int fd, const ModuleHeader_t* h, u32 address)
{
	u32 i, previous = 0;
	ModuleDiscovery_t* discovery = (void*)address;
	u32 exports = discovery->exports;
	if (discovery->magic != UYA_MODULE_DISCOVERY_MAGIC || discovery->version != UYA_DISCOVERY_VERSION ||
	    discovery->size != sizeof(*discovery))
		return 0;
	if (exports &&
	    ((exports & 3) || exports < h->address + sizeof(*discovery) || exports - h->address > h->memorySize - 4))
		return 0;
	for (i = 0; i < h->relocationCount; ++i) {
		ModuleRelocation_t r;
		u32 *word, value;
		if (!readExact(fd, &r, sizeof(r)) || (r.offset & 3) || r.offset < sizeof(ModuleDiscovery_t) ||
		    r.offset >= h->imageSize || (i && r.offset <= previous))
			return 0;
		previous = r.offset;
		word = (u32*)(address + r.offset);
		value = address + r.value;
		switch (r.type) {
			case 2:
				*word = value;
				break; /* R_MIPS_32 */
			case 4:
				*word = (*word & 0xfc000000U) | ((value >> 2) & 0x03ffffffU);
				break;
			case 5:
				*word = (*word & 0xffff0000U) | (((value + 0x8000U) >> 16) & 0xffffU);
				break;
			case 6:
				*word = (*word & 0xffff0000U) | (value & 0xffffU);
				break;
			default:
				return 0;
		}
	}
	if (!atEnd(fd))
		return 0;
	/* Linker LONG() has no ELF relocation record; all C exports use normal ones. */
	discovery->exports = exports ? address + (exports - h->address) : 0;
	return 1;
}

static int sameText(const char* a, const char* b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

static int endsWith(const char* text, const char* suffix)
{
	u32 n = strlen(text), m = strlen(suffix);
	return n >= m && sameText(text + n - m, suffix);
}

/* Accept name[.region][.bin]. Return 0 for another explicit region, -1 for
 * invalid input, or 1 with a canonical filename. Legacy modules/ is optional.
 * No arbitrary directories: all module images now live directly in mods/. */
static int resolveModuleName(const char* name, char* filename)
{
	char base[112];
	const char* region = UYA_REGION_NAME;
	u32 i;
	if (strlen(name) > 96)
		return -1;
	if (name[0] == 'm' && strlen(name) >= 8) {
		const char* prefix = "modules/";
		for (i = 0; i < 8 && name[i] == prefix[i]; ++i) {
		}
		if (i == 8)
			name += 8;
	}
	strcpy(base, name);
	if (endsWith(base, ".bin"))
		base[strlen(base) - 4] = 0;
	if (endsWith(base, ".ntscu"))
		region = "ntscu";
	else if (endsWith(base, ".pal"))
		region = "pal";
	else if (endsWith(base, ".ntscj"))
		region = "ntscj";
	else
		region = 0;
	if (region)
		base[strlen(base) - strlen(region) - 1] = 0;
	if (base[0] < 'a' || base[0] > 'z')
		return -1;
	for (i = 0; base[i]; ++i) {
		char c = base[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return -1;
	}
	if (region && !sameText(region, UYA_REGION_NAME))
		return 0;
	strcpy(filename, base);
	strcat(filename, "." UYA_REGION_NAME ".bin");
	return 1;
}

/* Allocate, read, relocate, then publish. BSS is zeroed before initialization. */
static void loadModule(const char* name, u32 modes, char names[MODULE_LIMIT][112])
{
	char path[128], filename[112];
	u32 i;
	ModuleHeader_t header;
	LoadedModule_t* module;
	int fd, valid;
	u32 address;

	u32 contexts = (modes & NAMED_CONTEXTS) ? (modes & ~NAMED_CONTEXTS) : modeContexts(modes);
	if (modes & NAMED_CONTEXTS)
		modes = (contexts & 1 ? 1 : 0) | (contexts & 6 ? 2 : 0);
	if (!modes || (modes & ~3U) || (contexts & ~UYA_CONTEXT_ALL))
		return; /* Disabled entries never open a file or reserve memory. */
	valid = resolveModuleName(name, filename);
	if (!valid)
		return; /* Explicit entries for other regions are not errors. */
	if (valid < 0) {
		printf("loader: invalid module name: %s\n", name);
		return;
	}
	/* First successfully loaded, enabled entry wins; aliases cannot run twice. */
	for (i = 0; i < STATE->count; ++i) {
		if (sameText(filename, names[i])) {
			printf("loader: duplicate module skipped: %s\n", name);
			return;
		}
	}
	if (STATE->count >= MODULE_LIMIT) {
		printf("loader: module limit reached: %s\n", name);
		return;
	}
	strcpy(path, "host0:uya/mods/");
	strcat(path, filename);
	fd = fileOpen(path, 1, 0);
	if (fd < 0) {
		printf("loader: open %s failed (%d)\n", path, fd);
		return;
	}
	if (!readExact(fd, &header, sizeof(header)) || !validHeader(&header)) {
		printf("loader: invalid module header/region or no free space: %s\n", name);
		fileClose(fd);
		return;
	}
	/* The list may narrow a binary's supported modes, never expand them. */
	if (modes & ~header.modes) {
		printf("loader: requested modes %u exceed supported modes %u: %s\n", modes, header.modes, name);
		fileClose(fd);
		return;
	}
	address = nextModuleAddress(header.memorySize);
	memset((void*)address, 0, header.memorySize);
	valid = readExact(fd, (void*)address, header.imageSize) && relocateModule(fd, &header, address);
	if (fileClose(fd) < 0)
		valid = 0;
	if (!valid) {
		memset((void*)address, 0, header.memorySize);
		printf("loader: incomplete image or invalid relocations: %s\n", name);
		return;
	}
	module = &STATE->modules[STATE->count++];
	strcpy(names[STATE->count - 1], filename);
	module->address = address;
	module->size = header.memorySize;
	module->update = 0;
	module->modes = modes;
	module->contexts = contexts;
	module->policy = UYA_CONTEXT_ALL;
	module->initialized = 0;
	module->init = header.initOffset == UYA_MODULE_NO_ENTRY ? 0 : (ModuleInit)(address + header.initOffset);
	if (header.updateOffset != UYA_MODULE_NO_ENTRY)
		module->update = (ModuleUpdate)(address + header.updateOffset);
	strcpy(MODULE_TABLE->entries[STATE->count - 1].name, filename);
	MODULE_TABLE->entries[STATE->count - 1].address = address;
	MODULE_TABLE->entries[STATE->count - 1].size = header.memorySize;
	MODULE_TABLE->entries[STATE->count - 1].modes = modes;
	MODULE_TABLE->count = STATE->count;
	syncCode();
	printf("loader: loaded %s at %08x (%u bytes)\n", name, address, header.memorySize);
}

static int isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

/* Grammar: name[.region][.bin] <contexts or legacy digit 0..3> [// comment].
 * Return 0 for a blank/comment line, 1 for an entry, -1 for invalid syntax.
 * Splitting in place keeps memory use small; paths cannot contain whitespace.
 * Full-line # comments remain accepted for existing comment-only lines.
 */
static int parseModuleLine(char* line, char** name, u32* modes)
{
	while (isSpace(*line))
		++line;
	if (!*line || *line == '#' || (line[0] == '/' && line[1] == '/'))
		return 0;
	*name = line;
	while (*line && !isSpace(*line))
		++line;
	if (!*line)
		return -1; /* Never assume a mode for an old bare-path entry. */
	*line++ = 0;
	while (isSpace(*line))
		++line;
	if (*line >= '0' && *line <= '3')
		*modes = *line++ - '0';
	else {
		u32 mask = 0;
		int special = 0;
		for (;;) {
			char *word = line, stop;
			u32 bit;
			while (*line && !isSpace(*line) && *line != ',' && *line != '/')
				++line;
			stop = *line;
			*line = 0;
			if (sameText(word, "campaign"))
				bit = UYA_CONTEXT_CAMPAIGN;
			else if (sameText(word, "local"))
				bit = UYA_CONTEXT_LOCAL;
			else if (sameText(word, "online"))
				bit = UYA_CONTEXT_ONLINE;
			else if (sameText(word, "all")) {
				bit = UYA_CONTEXT_ALL;
				special = 1;
			} else if (sameText(word, "disabled")) {
				bit = 0;
				special = 1;
			} else
				return -1;
			*line = stop;
			if ((mask & bit) || (special && mask))
				return -1;
			mask |= bit;
			while (isSpace(*line))
				++line;
			if (*line != ',')
				break;
			if (special)
				return -1;
			++line;
			while (isSpace(*line))
				++line;
		}
		*modes = mask ? (NAMED_CONTEXTS | mask) : 0;
	}
	while (isSpace(*line))
		++line;
	if (*line && *line != '#' && !(line[0] == '/' && line[1] == '/'))
		return -1;
	return validPath(*name) && strlen(*name) <= 96 ? 1 : -1;
}

/* Read once per boot. Accept LF/CRLF, blank lines and // comments. */
static int loadModuleList(void)
{
	char list[2048], names[MODULE_LIMIT][112]; /* Startup-only list and duplicate names. */
	char *line, *next, *name;
	u32 modes, lineNumber = 0;
	int parsed;
	u32 size = 0;
	int fd = fileOpen("host0:uya/config/modules.txt", 1, 0);
	int count = 0, valid = 1;
	/* The config list wins; the root list is a fallback, never merged. */
	if (fd < 0)
		fd = fileOpen("host0:uya/modules.txt", 1, 0);
	if (fd < 0) {
		printf("loader: modules.txt unavailable (%d)\n", fd);
		return 0;
	}
	while (size < sizeof(list) - 1) {
		count = fileRead(fd, list + size, sizeof(list) - 1 - size);
		if (count < 0 || (u32)count > sizeof(list) - 1 - size) {
			valid = 0;
			break;
		}
		if (!count)
			break;
		size += count;
	}
	if (size == sizeof(list) - 1 && !atEnd(fd))
		valid = 0;
	if (fileClose(fd) < 0)
		valid = 0;
	if (!valid || memchr(list, 0, size)) {
		printf("loader: modules.txt read failed or exceeds 2047 bytes\n");
		return 0;
	}
	list[size] = 0;
	line = list;
	while (*line) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		++lineNumber;
		parsed = parseModuleLine(line, &name, &modes);
		if (parsed < 0)
			printf("loader: invalid modules.txt line %u (expected path and contexts or 0..3)\n", lineNumber);
		else if (parsed && modes)
			loadModule(name, modes, names);
		if (!next)
			break;
		line = next;
	}
	return 1;
}

/* Called after the original sync function returns through entry.S.
 * entry.S preserves its return registers. The busy flag prevents recursive
 * entry while file I/O or a module itself waits for the game/IOP.
 */
void loaderTick(u32 layout)
{
	u32 i, mode;
	if (STATE->magic == LOADER_MAGIC && STATE->busy)
		return;
	if (STATE->magic != LOADER_MAGIC) {
		memset(STATE, 0, sizeof(*STATE));
		memset(MODULE_TABLE, 0, sizeof(*MODULE_TABLE));
		MODULE_TABLE->version = 1;
		MODULE_TABLE->entrySize = sizeof(ModuleInfo_t);
		MODULE_TABLE->magic = UYA_MODULE_TABLE_MAGIC;
		selectModulePool(platformMemorySize());
		STATE->magic = LOADER_MAGIC;
	}
	if (!platformReady(layout))
		return;
	/* Check the level against the resident SDK layout. The campaign main menu
     * (-1) is supported; unrecognized/pre-load states still defer callbacks.
     * The multiplayer lobby (0x27) is a valid multiplayer mode. */
	mode = platformMode(layout);
	if (!mode)
		return;
	STATE->busy = 1;
	STATE->mode = mode;
	platformApi(&loaderApi, mode);
	loaderApi.size = sizeof(loaderApi);
	loaderApi.context = platformContext(mode);
	loaderApi.setLifecycle = setLifecycle;
	loaderApi.restrictContexts = restrictContexts;
	loaderApi.publishService = publishService;
	loaderApi.findService = findService;
	loaderApi.loadConfig = moduleConfigLoad;
	loaderApi.saveConfig = moduleConfigSave;
	installExceptionHandler();

	/* A missing list can be retried on a later tick. Once accepted, restart
     * emulation to change modules: arbitrary patches cannot safely be undone.
     */
	if (!STATE->listLoaded && !STATE->retryTicks) {
		printf("loader: starting region %u, mode %u\n", loaderApi.region, mode);
		printf("loader: module pool %08x-%08x\n", STATE->poolStart, STATE->poolEnd - 1);
		if (loaderApi.warnings & UYA_WARNING_EXTRA_RAM_BIOS)
			printf("loader: incompatible BIOS for expanded RAM. Disable 128 MiB RAM or select a newer BIOS and "
			       "restart; ELF switching can still crash with modules in normal RAM.\n");
		STATE->listLoaded = loadModuleList();
		STATE->retryTicks = 300;
	}
	if (STATE->retryTicks)
		--STATE->retryTicks;
	/* Clean up all departing modules before any newly active module runs. */
	for (i = STATE->count; i > 0; --i) {
		LoadedModule_t* m = &STATE->modules[i - 1];
		if (m->active && !moduleAllowed(m)) {
			STATE->currentModule = i;
			if (m->deactivate)
				m->deactivate();
			m->active = 0;
		}
	}
	for (i = 0; i < STATE->count; ++i) {
		LoadedModule_t* module = &STATE->modules[i];
		if (!moduleAllowed(module))
			continue;
		STATE->currentModule = i + 1;
		if (!module->initialized) {
			int result = module->init ? module->init(&loaderApi) : 0;
			module->initialized = 1;
			if (result < 0) {
				printf("loader: init failed (%d) at %08x\n", result, module->address);
				u32 j;
				for (j = 0; j < 8; ++j)
					if (STATE->services[j].owner == i + 1)
						memset(&STATE->services[j], 0, sizeof(ModuleService_t));
				if (module->deactivate)
					module->deactivate();
				module->policy = 0;
				module->update = 0; /* Keep its memory reserved after failure. */
			}
			syncCode();
			if (result >= 0 && !moduleAllowed(module) && module->deactivate)
				module->deactivate();
		}
		if (!moduleAllowed(module))
			continue;
		if (!module->active) {
			module->active = 1;
			if (module->activate)
				module->activate();
		}
		if (moduleAllowed(module) && module->update)
			module->update();
		if (module->active && !moduleAllowed(module)) {
			if (module->deactivate)
				module->deactivate();
			module->active = 0;
		}
	}
	STATE->currentModule = 0;
	STATE->busy = 0;
}
