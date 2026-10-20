/* UYA host module loader. No heap resizing or game allocator patches.
 * Flow: choose the current game's API -> read modules.txt once -> validate and
 * load each image -> initialize/update only modules matching the active mode.
 * Memory: loader f0000-f2f80, modules f2f80-ff000, state ff000-100000.
 */
#include "module.h"
#include "config.h"
#include "exception.h"
#include "platform.h"
#include "strings.h"

#ifndef MODULE_START
#define MODULE_START 0x000f2f80U
#endif
#define MODULE_END 0x000ff000U
#define MODULE_LIMIT 8
#define LOADER_MAGIC 0x554d4c37U

struct LoadedModule {
    unsigned int address, size;
    char filename[112];
    ModuleUpdate update;
    ModuleInit init;
    unsigned int modes, initialized, contexts, policy, active;
    ModuleLifecycle activate, deactivate;
};

struct ModuleService {
    unsigned int id, version, owner;
    const void *table;
};
struct LoaderState {
    unsigned int magic, busy, count, mode, listLoaded, retryTicks, exceptionAttempted;
    struct LoadedModule modules[MODULE_LIMIT];
    ModuleApi api;
    int configBusy;
    char list[2048];
    unsigned int currentModule; /* index + 1; zero outside callbacks */
    struct ModuleService services[8];
};

#ifndef STATE
#define STATE ((struct LoaderState *)0x000ff000)
#endif
typedef char LoaderStateMustFit[(sizeof(struct LoaderState) <= 0x1000) ? 1 : -1];

const ModuleApi *moduleConfigApi(void) { return &STATE->api; }
int *moduleConfigBusy(void) { return &STATE->configBusy; }

#define api (STATE->api)
#define printf api.print
#define fileOpen api.open
#define fileRead api.read
#define fileClose api.close

/* Named list values carry a tag so legacy numeric modes retain their ABI. */
#define NAMED_CONTEXTS 0x100U
static unsigned int modeContexts(unsigned int mode)
{ return (mode&1?UYA_CONTEXT_CAMPAIGN:0)|(mode&2?(UYA_CONTEXT_LOCAL|UYA_CONTEXT_ONLINE):0); }
static int moduleAllowed(const struct LoadedModule *m)
{ return (m->modes & STATE->mode) && (m->contexts & m->policy & api.context); }
static int setLifecycle(ModuleLifecycle activate,ModuleLifecycle deactivate)
{
    struct LoadedModule *m;
    if(!STATE->currentModule) return -1;
    m=&STATE->modules[STATE->currentModule-1];m->activate=activate;m->deactivate=deactivate;
    return 0;
}
static int restrictContexts(unsigned int contexts)
{
    if(!STATE->currentModule || (contexts & ~UYA_CONTEXT_ALL)) return -1;
    STATE->modules[STATE->currentModule-1].policy=contexts;return 0;
}
/* Modules can export APIs without hard-coded addresses or load-order coupling.
 * Services are visible only while their owner's mode is active. */
static int publishService(unsigned int id, unsigned int version, const void *table)
{
    unsigned int i;
    if (!id || !version || !table || !STATE->currentModule) return -1;
    for (i = 0; i < 8; ++i) {
        struct ModuleService *s = &STATE->services[i];
        if (s->id == id) {
            if (s->owner != STATE->currentModule || s->version != version || s->table != table)
                return -1;
            return 0; /* Idempotent retry by the same publisher. */
        }
    }
    for (i = 0; i < 8; ++i) {
        struct ModuleService *s = &STATE->services[i];
        if (!s->id) {
            s->id=id; s->version=version; s->owner=STATE->currentModule; s->table=table;
            return 0;
        }
    }
    return -1;
}
static const void *findService(unsigned int id, unsigned int version)
{
    unsigned int i;
    for (i=0; i<8; ++i) {
        struct ModuleService *s=&STATE->services[i];
        if (s->id==id && s->version==version && s->owner &&
            moduleAllowed(&STATE->modules[s->owner-1])) return s->table;
    }
    return 0;
}

static int readExact(int fd, void *buffer, unsigned int size)
{
    unsigned int done = 0;
    while (done < size) {
        int count = fileRead(fd, (char *)buffer + done, size - done);
        if (count <= 0 || (unsigned int)count > size - done)
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
    api.flushCache(0);
    api.flushCache(2);
}

static void installExceptionHandler(void)
{
    unsigned char *memory = (unsigned char *)EXCEPTION_ADDRESS;
    unsigned int i, hash = 2166136261U;
    int fd, valid;

    /* Optional and attempted once per boot, before any module initialization.
     * Missing/invalid payloads never prevent normal module loading. */
    if (STATE->exceptionAttempted)
        return;
    STATE->exceptionAttempted = 1;
    fd = fileOpen("host0:uya/mods/debug/exceptionhandler.bin", 1, 0);
    if (fd < 0) {
        printf("loader: optional exception handler unavailable (%d)\n", fd);
        return;
    }
    for (i = 0; i < EXCEPTION_MEMORY_SIZE; ++i) {
        if (memory[i]) {
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
    EXCEPTION_INSTALL();
    syncCode();
    printf("loader: exception handler installed\n");
}

static int validPath(const char *path)
{
    const char *start = path;
    if (!*path)
        return 0;
    for (;;) {
        if (*path == ':' || *path == '\\')
            return 0;
        if (!*path || *path == '/') {
            int length = path - start;
            if (!length || (length == 1 && start[0] == '.') ||
                (length == 2 && start[0] == '.' && start[1] == '.'))
                return 0;
            if (!*path)
                return 1;
            start = path + 1;
        }
        ++path;
    }
}

static int validEntry(unsigned int offset, unsigned int size)
{
    return offset == UYA_MODULE_NO_ENTRY || (!(offset & 3) && offset < size);
}

/* Validate everything before writing executable memory, including overlaps. */
static int validHeader(const ModuleHeader *header)
{
    unsigned int i, end = MODULE_END;
    if (header->magic != UYA_MODULE_MAGIC || header->version != UYA_MODULE_VERSION ||
        (header->region && header->region != UYA_MODULE_REGION) ||
        (header->address & 63) || header->address < MODULE_START ||
        header->address >= end || !header->modes || (header->modes & ~3U) ||
        header->reserved || !header->imageSize ||
        (header->imageSize & 3) || header->imageSize > header->memorySize ||
        header->memorySize > end - header->address ||
        !validEntry(header->initOffset, header->imageSize) ||
        !validEntry(header->updateOffset, header->imageSize))
        return 0;
    for (i = 0; i < STATE->count; ++i) {
        struct LoadedModule *module = &STATE->modules[i];
        if (header->address < module->address + module->size &&
            module->address < header->address + header->memorySize)
            return 0;
    }
    return 1;
}

static int sameText(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int endsWith(const char *text, const char *suffix)
{
    unsigned int n = strlen(text), m = strlen(suffix);
    return n >= m && sameText(text + n - m, suffix);
}

/* Accept name[.region][.bin]. Return 0 for another explicit region, -1 for
 * invalid input, or 1 with a canonical filename. Legacy modules/ is optional.
 * No arbitrary directories: all module images now live directly in mods/. */
static int resolveModuleName(const char *name, char *filename)
{
    char base[112];
    const char *region = UYA_REGION_NAME;
    unsigned int i;
    if (strlen(name) > 96)
        return -1;
    if (name[0] == 'm' && strlen(name) >= 8) {
        const char *prefix = "modules/";
        for (i = 0; i < 8 && name[i] == prefix[i]; ++i) {}
        if (i == 8) name += 8;
    }
    strcpy(base, name);
    if (endsWith(base, ".bin")) base[strlen(base)-4] = 0;
    if (endsWith(base, ".ntscu")) region = "ntscu";
    else if (endsWith(base, ".pal")) region = "pal";
    else if (endsWith(base, ".ntscj")) region = "ntscj";
    else region = 0;
    if (region) base[strlen(base)-strlen(region)-1] = 0;
    if (base[0] < 'a' || base[0] > 'z') return -1;
    for (i = 0; base[i]; ++i) {
        char c = base[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return -1;
    }
    if (region && !sameText(region, UYA_REGION_NAME)) return 0;
    strcpy(filename, base);
    strcat(filename, "." UYA_REGION_NAME ".bin");
    return 1;
}

/* Images have a fixed linked address; BSS is zeroed before initialization. */
static void loadModule(const char *name, unsigned int modes)
{
    char path[128], filename[112];
    unsigned int i;
    ModuleHeader header;
    struct LoadedModule *module;
    int fd, valid;

    unsigned int contexts=(modes & NAMED_CONTEXTS)?(modes & ~NAMED_CONTEXTS):modeContexts(modes);
    if(modes & NAMED_CONTEXTS) modes=(contexts&1?1:0)|(contexts&6?2:0);
    if (!modes || (modes & ~3U) || (contexts & ~UYA_CONTEXT_ALL)) return; /* Disabled entries never open a file or reserve memory. */
    valid = resolveModuleName(name, filename);
    if (!valid) return; /* Explicit entries for other regions are not errors. */
    if (valid < 0) {
        printf("loader: invalid module name: %s\n", name);
        return;
    }
    /* First successfully loaded, enabled entry wins; aliases cannot run twice. */
    for (i = 0; i < STATE->count; ++i) {
        if (sameText(filename, STATE->modules[i].filename)) {
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
        printf("loader: invalid header, region, range or overlap: %s\n", name);
        fileClose(fd);
        return;
    }
    /* The list may narrow a binary's supported modes, never expand them. */
    if (modes & ~header.modes) {
        printf("loader: requested modes %u exceed supported modes %u: %s\n",
               modes, header.modes, name);
        fileClose(fd);
        return;
    }
    memset((void *)header.address, 0, header.memorySize);
    valid = readExact(fd, (void *)header.address, header.imageSize) && atEnd(fd);
    if (fileClose(fd) < 0)
        valid = 0;
    if (!valid) {
        memset((void *)header.address, 0, header.memorySize);
        printf("loader: incomplete or oversized module: %s\n", name);
        return;
    }
    module = &STATE->modules[STATE->count++];
    strcpy(module->filename, filename);
    module->address = header.address;
    module->size = header.memorySize;
    module->update = 0;
    module->modes = modes;
    module->contexts=contexts; module->policy=UYA_CONTEXT_ALL;
    module->initialized = 0;
    module->init = header.initOffset == UYA_MODULE_NO_ENTRY ? 0 :
        (ModuleInit)(header.address + header.initOffset);
    if (header.updateOffset != UYA_MODULE_NO_ENTRY)
        module->update = (ModuleUpdate)(header.address + header.updateOffset);
    syncCode();
    printf("loader: loaded %s at %08x (%u bytes)\n", name, header.address, header.memorySize);
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
static int parseModuleLine(char *line, char **name, unsigned int *modes)
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
    if(*line>='0' && *line<='3') *modes=*line++-'0';
    else {
        unsigned int mask=0; int special=0;
        for(;;) {
            char *word=line,stop;unsigned int bit;
            while(*line && !isSpace(*line) && *line!=',' && *line!='/') ++line;
            stop=*line;*line=0;
            if(sameText(word,"campaign")) bit=UYA_CONTEXT_CAMPAIGN;
            else if(sameText(word,"local")) bit=UYA_CONTEXT_LOCAL;
            else if(sameText(word,"online")) bit=UYA_CONTEXT_ONLINE;
            else if(sameText(word,"all")) {bit=UYA_CONTEXT_ALL;special=1;}
            else if(sameText(word,"disabled")) {bit=0;special=1;}
            else return -1;
            *line=stop;
            if((mask&bit) || (special && mask)) return -1;
            mask|=bit;
            while(isSpace(*line)) ++line;
            if(*line!=',') break;
            if(special) return -1;
            ++line;while(isSpace(*line)) ++line;
        }
        *modes=mask?(NAMED_CONTEXTS|mask):0;
    }
    while (isSpace(*line)) ++line;
    if (*line && *line!='#' && !(line[0]=='/' && line[1]=='/')) return -1;
    return validPath(*name) && strlen(*name) <= 96 ? 1 : -1;
}

/* Read once per boot. Accept LF/CRLF, blank lines and // comments. */
static int loadModuleList(void)
{
    char *line, *next, *name;
    unsigned int modes, lineNumber = 0;
    int parsed;
    unsigned int size = 0;
    int fd = fileOpen("host0:uya/config/modules.txt", 1, 0);
    int count = 0, valid = 1;
    /* The config list wins; the root list is a fallback, never merged. */
    if(fd<0) fd=fileOpen("host0:uya/modules.txt",1,0);
    if (fd < 0) {
        printf("loader: modules.txt unavailable (%d)\n", fd);
        return 0;
    }
    while (size < sizeof(STATE->list) - 1) {
        count = fileRead(fd, STATE->list + size, sizeof(STATE->list) - 1 - size);
        if (count < 0 || (unsigned int)count > sizeof(STATE->list) - 1 - size) {
            valid = 0;
            break;
        }
        if (!count)
            break;
        size += count;
    }
    if (size == sizeof(STATE->list) - 1 && !atEnd(fd))
        valid = 0;
    if (fileClose(fd) < 0)
        valid = 0;
    if (!valid || memchr(STATE->list, 0, size)) {
        printf("loader: modules.txt read failed or exceeds 2047 bytes\n");
        return 0;
    }
    STATE->list[size] = 0;
    line = STATE->list;
    while (*line) {
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        ++lineNumber;
        parsed = parseModuleLine(line, &name, &modes);
        if (parsed < 0)
            printf("loader: invalid modules.txt line %u (expected path and contexts or 0..3)\n", lineNumber);
        else if (parsed && modes)
            loadModule(name, modes);
        if (!next)
            break;
        line = next;
    }
    return 1;
}

/* Called at the template/Horizon return hook, with the stack restored.
 * entry.S preserves its return registers. The busy flag prevents recursive
 * entry while file I/O or a module itself waits for the game/IOP.
 */
void loaderTick(unsigned int layout)
{
    unsigned int i, mode;
    if (STATE->magic == LOADER_MAGIC && STATE->busy)
        return;
    if (STATE->magic != LOADER_MAGIC) {
        memset(STATE, 0, sizeof(*STATE));
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
    platformApi(&api, mode);
    api.size=sizeof(api); api.context=platformContext(mode);
    api.setLifecycle=setLifecycle;api.restrictContexts=restrictContexts;
    api.publishService = publishService;
    api.findService = findService;
    api.loadConfig=moduleConfigLoad;api.saveConfig=moduleConfigSave;
    installExceptionHandler();

    /* A missing list can be retried on a later tick. Once accepted, restart
     * emulation to change modules: arbitrary patches cannot safely be undone.
     */
    if (!STATE->listLoaded && !STATE->retryTicks) {
        printf("loader: starting region %u, mode %u\n", api.region, mode);
        STATE->listLoaded = loadModuleList();
        STATE->retryTicks = 300;
    }
    if (STATE->retryTicks)
        --STATE->retryTicks;
    /* Clean up all departing modules before any newly active module runs. */
    for(i=STATE->count;i>0;--i) {
        struct LoadedModule *m=&STATE->modules[i-1];
        if(m->active && !moduleAllowed(m)) {
            STATE->currentModule=i;
            if(m->deactivate) m->deactivate();
            m->active=0;
        }
    }
    for (i = 0; i < STATE->count; ++i) {
        struct LoadedModule *module = &STATE->modules[i];
        if (!moduleAllowed(module))
            continue;
        STATE->currentModule = i + 1;
        if (!module->initialized) {
            int result = module->init ? module->init(&api) : 0;
            module->initialized = 1;
            if (result < 0) {
                printf("loader: init failed (%d) at %08x\n", result, module->address);
                unsigned int j;
                for (j=0; j<8; ++j)
                    if (STATE->services[j].owner == i+1)
                        memset(&STATE->services[j], 0, sizeof(struct ModuleService));
                if(module->deactivate) module->deactivate();
                module->policy=0;
                module->update = 0; /* Keep its memory reserved after failure. */
            }
            syncCode();
            if(result>=0 && !moduleAllowed(module) && module->deactivate) module->deactivate();
        }
        if(!moduleAllowed(module)) continue;
        if(!module->active) { module->active=1; if(module->activate) module->activate(); }
        if(moduleAllowed(module) && module->update) module->update();
        if(module->active && !moduleAllowed(module)) {
            if(module->deactivate) module->deactivate();
            module->active=0;
        }
    }
    STATE->currentModule = 0;
    STATE->busy = 0;
}
