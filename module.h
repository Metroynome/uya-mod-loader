#ifndef UYA_MODULE_H
#define UYA_MODULE_H

#include "types.h"

#define UYA_MODULE_MAGIC             (0x4d415955)
#define UYA_MODULE_VERSION           (4)
#define UYA_MODULE_NO_ENTRY          (0xffffffff)
#define UYA_MODE_CAMPAIGN            (1)
#define UYA_MODE_MULTIPLAYER         (2)
#define UYA_MODE_BOTH                (3)
#define UYA_API_VERSION              (4)
#define UYA_CONTEXT_CAMPAIGN         (1)
#define UYA_CONTEXT_LOCAL            (2)
#define UYA_CONTEXT_ONLINE           (4)
#define UYA_CONTEXT_ALL              (7)
#define UYA_LOADER_DISCOVERY_ADDRESS (0x000a0000)
#define UYA_LOADER_DISCOVERY_MAGIC   (0x444c5955)
#define UYA_MODULE_DISCOVERY_MAGIC   (0x444d5955)
#define UYA_DISCOVERY_VERSION        (1)
#define UYA_MODULE_EXPORTS           __attribute__((section(".module.exports"), used))
#define UYA_MODULE_TABLE_MAGIC       (0x554d5442)
#define UYA_MODULE_LIMIT             (8)

/* Read only. Loaded does not imply active in the current execution context.
 * Entries remain valid until restart; do not call unknown code via address. */
#define UYA_LOADER_DISCOVERY ((const volatile loaderDiscovery_t*)UYA_LOADER_DISCOVERY_ADDRESS)
#define UYA_MODULE_TABLE     ((const volatile moduleTable_t*)UYA_LOADER_DISCOVERY->moduleTable)
/* Flat INI integer bindings: booleans use 0..1, choices use their index range.
 * Missing keys preserve defaults. Loading is transactional on parse failure. */
#define UYA_CONFIG_MAX_FIELDS      (32)
#define UYA_CONFIG_OK              (1)
#define UYA_CONFIG_MISSING         (0)
#define UYA_CONFIG_INVALID         (-1)
#define UYA_CONFIG_IO              (-2)
#define UYA_WARNING_EXTRA_RAM_BIOS (1)

typedef void (*ModuleLifecycle)(void);

/* REGION is set by Makefile.eeglobal: 1=NTSC-U, 2=PAL, 3=NTSC-J. */
typedef struct moduleHeader {
	u32 magic, version, region, address;
	u32 imageSize, memorySize, initOffset, updateOffset;
	u32 modes, relocationCount;
} moduleHeader_t;

/* Version 4 images are linked at a common base, then moved by the loader.
 * Relocations follow imageSize bytes; BSS and relocation records are not code. */
typedef struct moduleRelocation {
	u32 offset, type, value;
} moduleRelocation_t;

typedef struct moduleInfo {
	char name[112]; /* Canonical filename, e.g. forge.ntscu.bin. */
	u32 address, size, modes;
} moduleInfo_t;

typedef struct moduleTable {
	u32 magic, version, count, entrySize;
	moduleInfo_t entries[UYA_MODULE_LIMIT];
} moduleTable_t;

/* Fixed EE discovery layouts. Pointer fields are 32-bit EE addresses. */
typedef struct loaderDiscovery {
	u32 moduleTable, magic, version, size;
	u32 reserved[8];
} loaderDiscovery_t;

/* At moduleInfo_t.address. Zero exports means the module has no public table. */
typedef struct moduleDiscovery {
	u32 exports, magic, version, size;
} moduleDiscovery_t;

typedef struct moduleConfigField {
	const char* key;
	int* value;
	int minimum, maximum;
} moduleConfigField_t;

typedef struct moduleApi {
	u32 version, region, mode;
	int (*print)(const char* format, ...);
	int (*open)(const char* path, int flags, u16 mode);
	int (*read)(int fd, void* buffer, int size);
	int (*close)(int fd);
	void (*flushCache)(int operation);
	/* Services are discovered by ID and exact API version, not linked address.
     * publishService is valid only inside a module callback. The loader owns
     * the registry; pointers remain valid until emulation restarts. */
	int (*publishService)(u32 id, u32 version, const void* service);
	const void* (*findService)(u32 id, u32 version);

	/* API 3: execution context, independent of the campaign/MP library mode.
     * Registration is valid only inside this module's init/update callbacks.
     * The configuration is an upper bound; restrictContexts cannot expand it.
     * Deactivate releases all hooks/input/UI; activate runs on every re-entry. */
	u32 size, context;
	int (*setLifecycle)(ModuleLifecycle activate, ModuleLifecycle deactivate);
	int (*restrictContexts)(u32 contexts);
	/* API 4: optional persistence, independent of Mod Menu.
     * host0:uya/config/<mod>.<region>.ini; explicit synchronous operations. */
	int (*loadConfig)(const char* mod, const moduleConfigField_t* fields, int count);
	int (*saveConfig)(const char* mod, const moduleConfigField_t* fields, int count);
	/* Optional trailing field: check size before reading on older loaders. */
	u32 warnings;
} moduleApi_t;

/* Retain the pointer, not a copy: mode and function pointers change on a
 * campaign/multiplayer transition. init runs once, update runs per sync hook
 * (not necessarily once per video frame). Never retain game-object pointers
 * across level changes. */
typedef int (*ModuleInit)(const moduleApi_t* api);
typedef void (*ModuleUpdate)(void);
#endif
