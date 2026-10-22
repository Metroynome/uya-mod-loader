#ifndef UYA_MODULE_H
#define UYA_MODULE_H
#define UYA_MODULE_MAGIC 0x4d415955U
#define UYA_MODULE_VERSION 2U
#define UYA_MODULE_NO_ENTRY 0xffffffffU
#define UYA_MODE_CAMPAIGN 1U
#define UYA_MODE_MULTIPLAYER 2U
#define UYA_MODE_BOTH 3U
#define UYA_API_VERSION 4U
#define UYA_CONTEXT_CAMPAIGN 1U
#define UYA_CONTEXT_LOCAL 2U
#define UYA_CONTEXT_ONLINE 4U
#define UYA_CONTEXT_ALL 7U
typedef void (*ModuleLifecycle)(void);
/* REGION is set by Makefile.eeglobal: 1=NTSC-U, 2=PAL, 3=NTSC-J. */
typedef struct ModuleHeader {
    unsigned int magic, version, region, address;
    unsigned int imageSize, memorySize, initOffset, updateOffset;
    unsigned int modes, reserved;
} ModuleHeader;
/* Flat INI integer bindings: booleans use 0..1, choices use their index range.
 * Missing keys preserve defaults. Loading is transactional on parse failure. */
#define UYA_CONFIG_MAX_FIELDS 32
#define UYA_CONFIG_OK 1
#define UYA_CONFIG_MISSING 0
#define UYA_CONFIG_INVALID (-1)
#define UYA_CONFIG_IO (-2)
typedef struct ModuleConfigField {
    const char *key;
    int *value;
    int minimum, maximum;
} ModuleConfigField;

typedef struct ModuleApi {
    unsigned int version, region, mode;
    int (*print)(const char *format, ...);
    int (*open)(const char *path, int flags, unsigned short mode);
    int (*read)(int fd, void *buffer, int size);
    int (*close)(int fd);
    void (*flushCache)(int operation);
    /* Services are discovered by ID and exact API version, not linked address.
     * publishService is valid only inside a module callback. The loader owns
     * the registry; pointers remain valid until emulation restarts. */
    int (*publishService)(unsigned int id, unsigned int version, const void *service);
    const void *(*findService)(unsigned int id, unsigned int version);

    /* API 3: execution context, independent of the campaign/MP library mode.
     * Registration is valid only inside this module's init/update callbacks.
     * The configuration is an upper bound; restrictContexts cannot expand it.
     * Deactivate releases all hooks/input/UI; activate runs on every re-entry. */
    unsigned int size, context;
    int (*setLifecycle)(ModuleLifecycle activate, ModuleLifecycle deactivate);
    int (*restrictContexts)(unsigned int contexts);
    /* API 4: optional persistence, independent of Mod Menu.
     * host0:uya/config/<mod>.<region>.ini; explicit synchronous operations. */
    int (*loadConfig)(const char *mod, const ModuleConfigField *fields, int count);
    int (*saveConfig)(const char *mod, const ModuleConfigField *fields, int count);
} ModuleApi;
/* Retain the pointer, not a copy: mode and function pointers change on a
 * campaign/multiplayer transition. init runs once, update runs per sync hook
 * (not necessarily once per video frame). Never retain game-object pointers
 * across level changes. */
typedef int (*ModuleInit)(const ModuleApi *api);
typedef void (*ModuleUpdate)(void);
#endif
