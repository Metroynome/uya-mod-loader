# UYA Mod Loader

Standalone repository, moved from `uya-cheats/mod-loader`. Run the commands
below from this repository root (`D:/Repos/Metroynome/uya-mod-loader`).

Shared campaign/multiplayer loader, based on `sm-cheats/module-loader`, without
its heap edits. The same optional exception-handler payload is included. `main.c` contains the
commented loading, validation, and dispatch flow. This repository is independent
of the campaign and multiplayer cheat projects in `uya-cheats`.

## Enable or disable modules

The host filesystem layout keeps the editable list in `uya/config/`:

```text
hostfs/uya/
  config/modules.txt
  mods/
    debug/exceptionhandler.bin
    campaign-test.ntscu.bin
    campaign-test.pal.bin
    campaign-test.ntscj.bin
    multiplayer-test.ntscu.bin
    multiplayer-test.pal.bin
```

The packaged `build/dist/hostfs/uya/` uses the same layout. Entries in the list stay
as a base name such as `campaign-test`; the loader adds the running region and
`.bin` automatically. Explicit region suffixes are also supported.

Edit **`hostfs/uya/config/modules.txt`**:

```text
# Default production modules (tests/examples are opt-in)
modmenu          campaign,local
maploader.ntscu  local
maploader.pal    local
```

Use `campaign`, `local`, and `online`, joined with commas (spaces around commas
are allowed). `all` and `disabled` stand alone. Legacy `0` (disabled), `1`
(campaign), `2` (all multiplayer), and `3` (both) keep their original meaning.
Both `#` and `//` comments may appear on their own line or after an entry.
Unknown names, duplicate contexts, missing contexts, and malformed lines are
rejected. Names cannot contain whitespace. There is one shared list at
`host0:uya/config/modules.txt`, with at most eight images and 2047 bytes of list text.
Binary names and explicit regional suffixes are unchanged.

`local` includes the multiplayer main menu, Local Play staging, and offline
matches, including custom maps using online overlay IDs. `online` starts at the
post-DNAS profile-selection screen, before login: NetInfo reaches
`NETREGISTERED` (5), with `onlyLocalPlayers` clear. Local Play teardown can
also leave network state 5 or 10; the foremost active Local Play/landing menu keeps it
classified as local, while the online profile screen still disables offline modules. Returning to the multiplayer
main menu returns to `local`. Campaign includes its boot menu and Veldin (0).

Configuration narrows a binary's campaign/multiplayer library support; it cannot
make a campaign-only binary run in multiplayer. Default modules are configured
offline. Context filtering does not relocate the loader: running alongside the
full Horizon patch still has the documented memory overlap.

```text
campaign-test           1 // automatic region: campaign-test.<region>.bin
campaign-test.ntscu     1 // NTSC-U only
campaign-test.pal.bin   1 // PAL only; .bin is optional
campaign-test.ntscj     0 // disabled
```

Use either an automatic entry or individual regional entries for a given mod.
An explicit entry for another region is skipped without opening a file. A `0`
entry does not override an enabled automatic entry. If enabled entries resolve
to the same binary, only the first successfully loaded entry applies (including
its mode flags). The binary's own region and supported-mode checks still apply.
An optional `modules/` prefix is accepted for old lists, but no directory is
created for it. Names and suffixes are lowercase; unknown suffixes are rejected.

Disabled entries do not open files or reserve runtime module slots.

The selected modes must be supported by the binary's header. A both-mode binary
can be restricted to `1` or `2`. A singleplayer-only binary set to `2` or `3` is
rejected, not promoted into a multiplayer module. The same applies in reverse.
`3` covers singleplayer (including the initial main menu) and multiplayer.

Like the SM loader, this reads the list once per boot. **Restart emulation after
changing it.** This is not live unloading: a module may have patched game code,
so stopping its callback would not undo its effects. A missing/invalid list is
retried every 300 hook calls. A missing/invalid individual module is logged and
skipped. Restart after repairing one.

## Create a module

From a shell with GNU Make (or inside the Docker build container):

```sh
make new-module NAME=my-mod
make new-module NAME=campaign-mod LIBRARY=librac3
make new-module NAME=multiplayer-mod LIBRARY=libuya
make new-module NAME=shared-mod LIBRARY=both
```

Each command creates `modules/<name>/Makefile.defs` and commented starter C code,
and appends entries set to **`disabled`** to `modules.txt`, with comments
showing the available contexts. Existing folders are never
overwritten. The `hello` sample is enabled by default and only prints a message.

- `none`: one `main.c`, usable in both modes through the loader API.
- `librac3`: campaign-only `main.c`, with `<librac3/...h>` headers available.
- `libuya`: multiplayer-only `main.c`, with `<libuya/...h>` headers available.
- `both`: **one folder**, with `campaign.c` and `multiplayer.c`. These become
  `<name>-campaign.<region>.bin` and `<name>-multiplayer.<region>.bin`. Enable the campaign entry with `campaign` and the multiplayer entry with `local` or `online`.
  Each image links the correct library, avoiding collisions between identically
  named library functions and types. Do not link both archives into one image.

For a one-image module, `Makefile.defs` can specify `EE_OBJS` (space-separated relative
`.o` paths, built from `.c` or `.S`), `MODE` (`campaign`, `multiplayer`, or `both`), and `REGIONS`.
For a `both` library module, use `CAMPAIGN_OBJS` and `MULTIPLAYER_OBJS`
to add more files to each image. The generated multiplayer callback includes
`uyaPreUpdate()` / `uyaPostUpdate()` to maintain library input state.

Each module uses ordinary Make variables, matching the cheat repositories:

```make
LIBRARY = none
MODE = both
REGIONS = ntscu pal ntscj
EE_OBJS = main.o settings.o
```

Its `Makefile` contains only `include ../../Makefile.eeglobal`. Add C/assembly
files to `EE_OBJS`; shared rules handle compiling, library linking and regions.
`LIBRARY=both` uses `CAMPAIGN_OBJS` and `MULTIPLAYER_OBJS` for the two images.
Private modules using the former `module.json` or `module.conf` should move their
settings to `Makefile.defs` and rebuild the complete module set once.

The root `Makefile` provides the build commands and allocates module addresses.
`Makefile.eeglobal` contains the shared EE compiler, library and linker rules.
Only module creation and release packaging use shell helpers. The loader uses
`bin2code` plus regional hooks in `template/` to produce PNACH files only.
Modules are plain binaries; the loader validates them at runtime. No Python is needed.
For runtime placement, `tools/relocate.c` packages the ELF's MIPS relocation
records into the module binary. `tools/sections.awk` preserves the normal link's
live sections because the older EE linker cannot retain relocations and discard
unused code correctly in one pass. Neither utility chooses module addresses.

`moduleInit(const ModuleApi *)` runs once, the first time its mode is active.
A negative result disables its update callback while retaining its memory.
`moduleUpdate(void)` runs on subsequent matching return hooks, including the first
one after initialization. Hooks can run more than once per video frame. Check
level/player readiness before using game APIs; do not retain pointers into a
previous level. Module BSS is zeroed at load time and persists across mode changes.
The supplied API pointer remains valid and is updated when the mode changes.
Use `api->print/open/read/close/flushCache` for mode-correct resident SDK services.

## Extensible mod menu

The supplied `modmenu` module exposes a versioned API so other mods can register
tabs, toggles, integer ranges and actions without editing the menu itself.
`menu-example` demonstrates an independent consumer. Both are enabled in the
sample list. Press **L3 + R3** to open; use L1/R1 for tabs, the D-pad to
navigate, Cross to select and Triangle to close.

The menu is independent of network connectivity and supports campaign plus
NTSC-U/PAL multiplayer. NTSC-J multiplayer retains the loader limitation below.
See [the menu API guide](modules/modmenu/README.md) for integration and validation
details.

**The loader uses runtime API 4 with binary header version 4. For this upgrade,
rebuild all modules and deploy them with the new PNACH, then cold-boot.**
Old version-1/2/3 binaries are rejected. Subsequent version-4 modules can be replaced independently.
The loader now supplies `publishService` / `findService` for versioned APIs.
Service IDs belong to their publishing module; lookup requires an exact API
version and an allowed owner mode/context. Failed initialization removes that module's
services. Consumers should retry lookup when a provider is not yet available.

## Build

The independent `maploader` module adds a Custom Maps tab using Horizon-format
hostfs map packs. See [maploader setup and current support](modules/maploader/README.md).
Local staging selects the retail base automatically. Custom maps use one scrolling
list with None and X to select. Maploader disables itself on an online lobby
connection by default; `maploader-online.txt` provides an explicit future-server
opt-in. Campaign replacement is not yet implemented.

Requires a POSIX shell, GNU Make, a host C compiler, `bin2code`, and the PS2 EE/IOP toolchain. Release packaging also uses zip/unzip and sha256sum. No Python is needed. Set `PS2SDK` and put
`ee-gcc`, `ee-nm`, and `ee-objcopy` on PATH. `EE_PREFIX` overrides `ee-`.

```sh
make                                       # all three regions
make REGION=ntscu                          # one region
```

Modules receive nonoverlapping, 64-byte-aligned addresses at runtime, in
`modules.txt` order. Each module is built independently at the same link base;
the loader adjusts its internal references after assigning space. Creators do
not coordinate addresses or rebuild each other's modules. All declared modules
are built; only enabled entries consume runtime memory. The limit remains eight
modules. Space starts at the next 64-byte boundary after `_loader_end` (code,
module table, and persistent state) and ends at `0x00100000`. An installed
exception handler lives separately at `0x00098000-0x0009e000`, below the loader,
so enabling it does not change the module pool.
Allocations last until restart;
individual unloading, compaction, and heap resizing are not implemented.

Library defaults match this workspace:

- `LIBRAC3_DIR`: `Metroynome/librac3`
- `LIBUYA_DIR`: `Horizon/horizon-uya-patch/libuya`

Set these environment variables for another checkout. Library builds happen in
isolated per-region copies under `build/`, leaving the library sources alone.
Unused library sections are discarded so unrelated unfinished functions do not
prevent a module from linking. Actual functions you call must still be implemented.

## Docker (no local library checkouts needed)

From the repository directory, build the toolchain image once:

```powershell
docker build -t uya-mod-loader .
docker run --rm -v "${PWD}:/workspace" uya-mod-loader
```

The Dockerfile runs `docker-init.sh` to install Make, host C/build utilities, and clone pinned
revisions of `Metroynome/librac3` and `Horizon-Private-Server/horizon-uya-patch (libuya)` into the image. The
container's default command builds both the loader cheat and all configured
modules for all three regions. Outputs remain in this repository's `build/`,
`hostfs/uya/mods/`, and `release/` folders. No sibling library mounts are needed.
Network access is needed when building the image; normal module builds use the
cloned sources already in the image.

```powershell
# Build just NTSC-U:
docker run --rm -v "${PWD}:/workspace" uya-mod-loader make REGION=ntscu
# Create a module, then enable its entry in hostfs/uya/config/modules.txt and rebuild:
docker run --rm -v "${PWD}:/workspace" uya-mod-loader make new-module NAME=my-mod LIBRARY=librac3
```

For intentional library upgrades, pass `--build-arg LIBRAC3_REF=<commit>` and/or
`--build-arg LIBUYA_REF=<commit>` to `docker build`. Library region restrictions
still apply. The build copies library sources into `build/` before compiling.

`docker-init.sh` can also initialize a fresh base-image container and run a
command with the dependency paths exported:

```powershell
docker run --rm -v "${PWD}:/workspace" -w /workspace --entrypoint sh dnawrkshp/ps2dev-libdl:latest ./docker-init.sh make
```

Running `sh docker-init.sh` alone prepares dependencies without building. It
refuses to reset existing modified or mismatched checkouts. Use it inside the
Linux container, not directly in Windows PowerShell.

## Make workflows inside the container

After `sh docker-init.sh`, you can run these from the repository root:

```sh
make clean && make             # loader cheat and every configured module
make modules                   # same complete build, including loader
make module NAME=hello         # rebuild one source module folder
make module NAME=hello REGION=ntscu
cd modules/hello
make clean && make             # only this module, all its supported regions
make REGION=ntscu
```

`make clean` at the root removes the generated build directory and regional
hostfs binaries. It keeps module sources, `modules.txt`, and the optional debug
payload. A module-folder clean removes only that module's objects/binaries.
Generated module folders include a Makefile.

Single-module builds need no existing layout or full-set build. A `both` source
folder rebuilds both of its library variants. Replace that module's binary and
restart emulation to load it; the loader assigns space based on its current size.
A fresh project still needs the root build to produce the loader PNACH itself.

To work interactively using the Dockerfile image:

```powershell
docker run --rm -it -v "${PWD}:/workspace" uya-mod-loader sh
```

The image is already initialized. The dependency directories are also discovered
automatically by the build tool, so running `sh docker-init.sh` in a base-image
shell does not require sourcing the script before subsequent `make` commands.

## Use in PCSX2

After a successful build:

1. Use `build/dist/mod-loader.<region>.pnach` as the cheat contents for your game's
   CRC (PCSX2 shows it in the log). Enable cheats. Do not enable another cheat
   that occupies the loader/module memory range or replaces the same hooks.
2. Enable PCSX2 host filesystem access and point its host root at
   `build/dist/hostfs`, so `host0:uya/config/modules.txt` resolves to the packaged list.
3. Edit the packaged `build/dist/hostfs/uya/config/modules.txt` if needed, then cold-boot.
   If you rebuild, the source list in `hostfs/uya/config/modules.txt` replaces that copy.

The loader uses host filesystem access, as the SM loader does. It is not a disc
or memory-card module installer. Switching campaign/multiplayer selects the
matching callbacks automatically. Level IDs `1..0x26` select campaign and
`0x27` and above select multiplayer (including the lobby). Campaign ID `-1`
is the initial main menu and runs campaign and both-mode modules. ID `0` and
other negative IDs defer loading/callbacks as pre-load or unknown states.
Modules must check player/level readiness before calling gameplay functions.
The hook's resident SDK layout must agree with the level classification;
no separate campaign and multiplayer loader
patches need to be enabled.

## Optional exception handler

`hostfs/uya/mods/debug/exceptionhandler.bin` is the SM/Horizon payload relocated to `0x98000` (10,356 bytes), with drawing scratch inside its reservation. When present, the build copies it to
`build/dist/hostfs/uya/mods/debug/exceptionhandler.bin`. The loader attempts installation
once per boot, on its first valid menu/gameplay/lobby hook, before initializing modules.
It is separate from `modules.txt` and does not consume a module slot.

To disable it, remove or rename the file in the hostfs root you use and cold-boot.
To keep it disabled across builds, remove/rename the source file too; the next
build removes any stale packaged copy. Missing files simply log a message and
normal module loading continues. Restart after adding it to enable it again.

Installation requires the whole `0x00098000-0x0009e000` range to be empty and
checks the exact file size and FNV-1a hash before executing it. Corrupt, truncated,
oversized, or unreadable payloads are skipped; occupied memory is left untouched.
The binary patches kernel exception handlers, as in SM. PAL cold boot and a controlled exception verified installation, register capture,
and bounded rendering. The user confirmed the exception/register screen is readable.

## Regions and validation status

| Component | NTSC-U | PAL | NTSC-J |
|---|---|---|---|
| Loader + API-only module build | Passed | Passed | Passed |
| `librac3` module link test | Passed | Passed | Passed |
| Supplied `libuya` module link test | Passed | Passed | No regional library supplied |

The scaffold restricts `libuya` and `both` modules to NTSC-U/PAL by default.
NTSC-J is **not** silently compiled with NTSC-U multiplayer addresses. To add it,
supply a genuinely ported `libuya` with `Makefile.ntscj` and add `ntscj` to the
module's region list. Library-specific address accuracy remains the library's
responsibility; defining a region macro does not port a function.

Campaign SDK addresses were checked against the supplied three retail boot-core
files and an NTSC-U campaign memory image. Multiplayer SDK addresses were checked
against all 22 supplied NTSC-U/PAL multiplayer lobby/map memory dumps, including
regional level-ID globals. Campaign level globals were checked in the NTSC-U
memory dumps and matched to PAL/NTSC-J extracted level code. **NTSC-J multiplayer
runtime addresses have not been verified; its callbacks are deferred rather
than reading an NTSC-U global.** Unknown SDK layouts are rejected by the runtime
signature checks. See [ADDRESSES.md](ADDRESSES.md).

No live emulator gameplay test has been performed. The generated patches still
need cold-boot testing in campaign and multiplayer, level transitions, and mode
switches on the intended regional discs. Do not interpret successful compilation
or host tests as an in-game test result.

## Memory and tests

No heap base/size edits or heap allocation. The optional exception handler uses
its own low-memory range only when installed:

| Range | Use |
|---|---|
| `0x00098000-0x0009e000` | Optional exception handler, 24 KiB including workspace |
| `0x000a0000-0x000a0030` | Fixed 48-byte loader discovery block |
| `0x000a0030` to `_loader_end` | Loader code/data, module table, and persistent state |
| `0x0009e000-0x000a0000` | Unused 8 KiB gap |
| Aligned `_loader_end` to `0x00100000` | Modules, including BSS and Forge's mailbox |

Headers include ABI version, region, mode mask, link address, sizes, and entry
point offsets. The loader rejects invalid ranges, oversized/truncated images,
wrong regions, overlapping modules, invalid entry points, and path traversal.
Memory is cleared before loading; data/instruction caches are synchronized before
executing new code. State is outside PNACH-injected bytes so repeated cheat writes
do not reset it. This area must remain free of other injected patches.

Version-4 module binaries contain a 40-byte file header, a 16-byte discovery
header at the loaded base, code/data, then relocation records. The loader handles MIPS 32-bit pointers, jumps, and paired high/low
address instructions; absolute game addresses remain unchanged. Older version-1/2/3
binaries are rejected: rebuild them once with this version's module Makefiles.
Unsupported relocation types fail packaging instead of producing broken code.

Other modules can inspect `UYA_MODULE_TABLE` from `module.h`, discovered through
the pointer at `0x000a0000`. The table itself is linker-placed.
Check `magic == UYA_MODULE_TABLE_MAGIC`, `version == 1`, `entrySize == sizeof(ModuleInfo)`,
and `count <= UYA_MODULE_LIMIT` before reading `entries[0..count-1]`. Each entry
has a canonical filename, base `address`, allocated `size` (including BSS), and
configured `modes`. These are loaded modules, not necessarily currently active.
Never modify this table. Use the existing service APIs to call another mod's
published functions; a module base address is not a function pointer.

The loader and modules share `0x000a0000-0x000fffff` (384 KiB total).
The loader reserves only its actual linked size; rebuilding a larger or smaller
loader automatically shifts module placements on the next cold boot.
The optional handler remains fixed below the loader. Failed loads do not consume module space. See ADDRESSES.md for its relocation and drawing workspace.
Rebuild the Docker image after updating: the pinned libuya revision moves its
network callback pointer out of this block. Older Horizon patch/downloader/game
mode binaries still using `0x000a0000` must also be replaced before coexistence.
Relocation alone does not resolve shared hooks or implement online unloading.
Replace the old PNACH files and fully restart the game; do not load an old savestate.

`make release` builds every region and validates the generated ZIPs.
Build-time binary size limits are omitted; runtime module checks remain. Relocation
packaging verifies that the retained-relocation link has the same payload as the
normal link, to account for the old toolchain's behavior.
Local native C regression tests run with `make test`; local tests remain excluded
from version control by this project's `.gitignore`. The game runtime and
modules are C; compilation and linking use shared Make rules.
Verified regional address tables are checked in and require no generation step.
Runtime signature guards remain enabled.

Library-backed modules can include `<librac3/stdio.h>` or `<libuya/stdio.h>`
and call `printf` for PCSX2 console output. Campaign `printf` uses the verified
SDK console routine at `0x00121760`; multiplayer uses `0x00128c70`.
Library-free modules can use `api->print`, which follows the active mode.

## Context and lifecycle API (loader API 3)

Mods need only `module.h`; the Mod Menu API is optional. `api->mode` continues to
select campaign versus multiplayer libraries. `api->context` reports exactly one
of `UYA_CONTEXT_CAMPAIGN`, `UYA_CONTEXT_LOCAL`, or `UYA_CONTEXT_ONLINE`.

```c
static const ModuleApi *loader;
static void activate(void) { /* Reinstall this mod's hooks if needed. */ }
static void deactivate(void) { /* Restore owned hooks, release input, remove UI. */ }
int moduleInit(const ModuleApi *api)
{
    if (api->version < UYA_API_VERSION || api->size < sizeof(*api)) return -1;
    loader = api;
    api->setLifecycle(activate, deactivate);
    api->restrictContexts(UYA_CONTEXT_CAMPAIGN | UYA_CONTEXT_LOCAL);
    return 0;
}
```

`restrictContexts` is optional and can only narrow the configuration. Register
callbacks from your own init/update callback. Init runs once; activation runs
before the first update and on re-entry. Deactivation runs once when permission
ends, before newly enabled modules run. The API already describes the new
context during cleanup. Do not assume old game overlays are still present:
restore only hooks you still own. Memory stays reserved and services disappear
from discovery while their provider is disabled. Modules must implement their
own cleanup for persistent changes; skipping updates cannot undo arbitrary code.

Deploy the new loader cheat and rebuilt modules together, then restart emulation.
The binary header is version 4; the callback API remains version 4.
Older modules that require runtime version exactly 2 must be rebuilt/updated.

## Discovering modules and exported data

Read the fixed discovery block at `0x000a0000` before following any pointers.
All fields are unsigned 32-bit little-endian EE values:

| Offset | Loader discovery field |
|---|---|
| 0x00 | Pointer to the current module table |
| 0x04 | Magic `0x444C5955` (`UYLD`) |
| 0x08 | Discovery version 1 |
| 0x0C | Size: 48 bytes |
| 0x10-0x2C | Eight reserved words, zero |

The linker places the table and persistent state after the loader payload.
Their addresses can change when the loader is rebuilt; never hard-code
`0xAF800` or `0xAFD00`. In C, use `UYA_MODULE_TABLE` after checking
`UYA_LOADER_DISCOVERY`. The table retains magic `0x554D5442`, version 1,
count (maximum 8), and entrySize 124, followed by its existing entries.
The table is initialized on the loader's first valid tick, so wait for its header.
Code begins after the discovery block; PNACH hooks use the linked entry symbols.
The loader, table, and state must still fit below `0xB0000`.

Each entry's address points to a standard 16-byte module discovery header:

| Offset | Module discovery field |
|---|---|
| 0x00 | Optional exports pointer; zero when there are no exports |
| 0x04 | Magic `0x444D5955` (`UYMD`) |
| 0x08 | Discovery version 1 |
| 0x0C | Size: 16 bytes |

The existing linker rules generate this header for every module. Its fields
are read-only to clients. Entry offsets in the file header point past it to code.
The runtime rebases its exports pointer; C pointers inside an exports structure
are handled by the existing relocation records.

Creators can optionally define the single global symbol `moduleExports` in
one C file. Give the structure a mod-specific magic, version and size so clients
can recognize it, then add the pointers they need. For example:

```c
#include "module.h"

#define MY_EXPORTS_MAGIC 0x3158454dU

typedef struct MyExports {
	unsigned int magic, version, size;
	volatile unsigned int* status;
} MyExports;

static volatile unsigned int status;
const MyExports moduleExports UYA_MODULE_EXPORTS = {
	MY_EXPORTS_MAGIC, 1, sizeof(MyExports), &status};
```

No extra files, build scripts, assembly, or Mod Menu dependency are needed.
Omit the definition to leave exports null. Keep exported storage inside the
module for its entire lifetime; never export a stack address. This discoverable
data does not replace context-aware `publishService` / `findService` APIs.

Before reading exports, validate the discovery headers and pointer bounds within
the module's address/size. Loaded does not mean active: a client must follow the
module's own readiness protocol. Rediscover after restart, module replacement,
or savestate load; do not retain addresses across sessions. Forge's complete
lookup and heartbeat protocol is in [PINE.md](modules/forge/PINE.md).

## Mod configuration files

The loader's `ModuleApi` provides `loadConfig` / `saveConfig` independently of
Mod Menu. Settings live at `host0:uya/config/<mod-name>.<region>.ini`.
See [configuration API usage and return codes](CONFIG.md). The Example Mod tab
provides Save/Load actions. Preserve the config directory when updating.

Module lists may be placed in the host's `uya/` root or `uya/config/`. Lookup
order is `config/modules.txt`, then `modules.txt`. The first file that opens wins; lists are never merged,
and a malformed config list does not silently fall back. Module binaries remain
under `uya/mods/` regardless of the list's location. The build and new-module
command support either location.

Install matching loader cheats and module binaries, then cold-boot without a
savestate. This update changes the loader/module boundary within the existing
reserved memory. Move existing `.ini` files from `uya/mods/config/` to
`uya/config/`; files are not read from the old location.


## Release packages and GitHub Actions

Run `make release` in the initialized Docker environment to build all regions
and create `release/zips/uya-mod-loader-{ntscu,pal,ntscj,all}.zip` plus SHA-256
checksums. Each ZIP contains matching loader cheats, production modules, default
`config/modules.txt`, setup instructions, and an optional exception handler.
The combined ZIP uses explicit region suffixes to skip unsupported binaries.
NTSC-J includes campaign Mod Menu; Map Loader is available for NTSC-U/PAL.

The tracked [release guide](release/README.md) explains installation. Published PNACHs and production host files are tracked in `release/`;
development binaries and ZIPs remain ignored. [release/modules.txt](release/modules.txt) controls
which modules enter the ZIPs; tests/examples remain available in source but are
excluded from the release defaults and archives. Local INIs and downloaded map
packs are never included in ZIPs. `make clean` preserves all release files; `make clean-release` removes generated
release outputs while retaining the guide and policies.

The [Release workflow](.github/workflows/release.yml) builds the Docker image,
packages all regions, verifies the ZIPs, and uploads a downloadable artifact.
Run it manually from Actions, or push a `v*` tag to attach the ZIPs to a draft
GitHub release. Review and publish that draft when ready. Rerunning a tag build
replaces its matching assets using [GitHub CLI release upload](https://cli.github.com/manual/gh_release_upload).


Release PNACHs use `SERIAL_CRC.pnach` names and include `[UYA Mod Loader]` and
`author=Metroynome`, matching the rac-cheats release convention. Install both
campaign and multiplayer files for NTSC-U/PAL; see the
[verified CRC table](release/README.md#verified-game-crcs). Normal builds use
`build/dist/` so development builds cannot silently replace published files.

## Release layout and mod utilities

`make release` creates region ZIPs in `release/zips/`. Their root contains `uya/`,
the PNACH files, a short README, and Horizon's Bash/PowerShell map updaters.
Set the host root to the folder containing `uya/`. Detailed API documentation
stays in the repository. The updaters must run from that same host root.

Supporting files belong in `uya/mods/utils/`; Map Loader opens
`host0:uya/mods/utils/mapscan.irx`. Other mods may put their own named files or
subfolders there. Utilities are not module-list entries. Module binaries stay
in `uya/mods/`, and saved settings stay in `uya/config/`.

Forge editor integration: see [PINE command protocol](modules/forge/PINE.md).
