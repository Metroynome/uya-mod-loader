# Mod configuration API

Configuration persistence is part of loader API 4. No Mod Menu, tab, or service
lookup is required. A mod supplies defaults, then loads overrides once during
initialization:

```c
#include "module.h"
static const ModuleApi *loader;
static int enabled = 1, amount = 5;
static const ModuleConfigField settings[] = {
    { "enabled", &enabled, 0, 1 },
    { "amount", &amount, 0, 10 }
};
int moduleInit(const ModuleApi *api)
{
    if (api->version < 4 || api->size < sizeof(ModuleApi)) return -1;
    loader = api;
    int result = loader->loadConfig("my-mod", settings, 2);
    /* Missing files retain defaults; log invalid files or I/O errors. */
    if (result < 0) loader->print("my-mod: config error %d\n", result);
    return 0;
}
/* Call when the user commits settings, not every frame. */
static int saveSettings(void)
{
    return loader->saveConfig("my-mod", settings, 2);
}
void moduleUpdate(void) {}
```

For NTSC-U this uses `host0:uya/config/my-mod.ntscu.ini`, corresponding to
`hostfs/uya/config/my-mod.ntscu.ini` in the repository/release layout:

```ini
[settings]
enabled=1
amount=5
```

PAL uses `.pal.ini`; NTSC-J uses `.ntscj.ini`. Campaign and multiplayer share
the same regional file. Keep the packaged `config` directory: the API does not
create directories. Move prior settings from `uya/mods/config/` into `uya/config/`.
No implicit writes occur; a missing file stays missing until the mod calls save.
A creator may ship a default INI, or simply use defaults in code.

Bindings support signed 32-bit integers. Use 0..1 for booleans and an option
index range for choices; strings and floats are not supported. Missing keys
retain current values. Unknown keys are ignored. Known keys must be unique,
contain decimal integers, and satisfy their declared ranges. A failed load
leaves all bound settings unchanged. Blank lines, CRLF, `#`/`;` comments, and
an optional `[settings]` header are accepted.

| Result | Meaning |
| --- | --- |
| `UYA_CONFIG_OK` (1) | Completed successfully |
| `UYA_CONFIG_MISSING` (0) | File absent; defaults retained |
| `UYA_CONFIG_INVALID` (-1) | Invalid names, bindings, or contents |
| `UYA_CONFIG_IO` (-2) | Open/read/write/close failure or oversized input |

Names accept letters, digits, underscores, and hyphens: mod names are 1..48
characters and keys 1..31. Maximum 32 fields and 2047 input bytes. Operations
are synchronous; call them from module callbacks or explicit actions, never
every update. Saves validate settings before truncation and replace the whole
file, discarding comments and unknown keys. Writes are not atomic; an I/O error
can leave an incomplete file, so check the return value.

Existing Mod Menu configuration calls forward to this API. New mods should use
`ModuleConfigField`, `UYA_CONFIG_*`, and the loader functions directly.

Campaign's private SDK writer is mapped to resident support routines for each
region. Verify its assembly with
`python tools/map_config_writer.py <rac3-extraction> --check`.
Multiplayer uses the resident SDK writer. Neither path changes the game heap.
