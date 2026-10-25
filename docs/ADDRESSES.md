# Runtime address evidence

The campaign and multiplayer cores are different layouts. Never substitute
librac3's symbols for multiplayer or libuya's symbols for campaign.

| Function / return site | Campaign | Multiplayer |
|---|---|---|
| scePrintf | 0x00121760 | 0x00128c70 |
| sceOpen | 0x00123430 | 0x0012aa38 |
| sceClose | 0x001236c0 | 0x0012acc8 |
| sceRead | 0x00123840 | 0x0012b080 |
| FlushCache | 0x0011f0a0 | 0x001264e0 |
| Loader sync entry hook | 0x0012c908 | 0x00139e08 |
| Original return / Horizon hook (untouched) | 0x0012c994 | 0x00139e94 |

Campaign sources: `boot/core1/rac3.boot.{ntscu,pal}.v100.retail.core.0001.bin`
and `rac3.boot.ntscj.v103.retail.core.0001.bin` in the supplied RAC3 extraction.
Core1 loads at 0x00116f80; the FlushCache syscall stub anchors this mapping.
NTSC-U addresses were also compared with Ghidra's `ntscu.07.AnniNation.bin`.
The three regional files retain the same function/return offsets listed here.

Multiplayer source: `eemem/ntsc.39.PreLoad_MultiplayerMenu.bin`, plus existing
libuya fixed SDK stubs and the Horizon multiplayer patch hook. The
multiplayer Ghidra database has several functions relocated into debug memory;
the original sceOpen/sceClose locations were recovered from the unmodified
supplied memory dump, not copied from those relocated labels.
PAL and NTSC-U multiplayer SDK signatures and hooks also match all 22 supplied
`eemem/multiplayer/` lobby/map dumps. These were found in the nested directory
after the initial implementation. NTSC-J multiplayer is still unverified.

The sync entry sequences were checked against all three campaign core1 files
and all 22 NTSC-U/PAL multiplayer lobby/map dumps. The first instructions are
`addiu sp,sp,-32; sd s0,0(sp); sd ra,16(sp); jal helper`. PNACH replaces only
the first two instructions with J/NOP, guarded by the untouched sd ra and jal
words at entry+8 and entry+12. NTSC-J emits only the campaign hook.

`entry.S` saves the original caller's ra/gp, calls a trampoline which replays
the displaced prologue, and resumes the original function at entry+8. After
that function returns, it saves the full v0/v1 values, runs loaderTick, restores
those values and ra/gp, and returns to the original caller. The original
function restores s0 and its own stack. The wrapper reserves another 64 bytes.
Module callbacks retain the old post-sync timing and call frequency.

The return instructions at 12C994/139E94 are no longer written by the loader.
An existing Horizon return hook can therefore execute before control returns
to the loader wrapper. This removes the shared hook-site conflict; it does not
establish compatibility with Horizon's other memory reservations and hooks.
Fully restart emulation when upgrading from the old return-hook build, so its
old in-memory jump is cleared rather than calling the new wrapper recursively.

All regional builds and existing native checks pass. PAL cold boot loaded all
eight configured modules with the entry wrapper. The user confirmed boot-menu
drawing and entry to Local Play staging. Forge selected Aquatos Aquadome,
started the game, reloaded it, hopped to retail Bakisi Isles, and hopped back;
all requests completed with state 3/error 0 and the expected level IDs.
Live reads confirmed the original jr ra / stack restore at 12C994 during
campaign boot and 139E94 in multiplayer. The user confirmed gameplay, HUD,
and in-game Mod Menu drawing/navigation.
NTSC-J live testing remains unverified. Later NTSC-U boot and online results are recorded below.

## Gameplay mode versus resident layout

The entry wrapper identifies the resident SDK layout. Gameplay mode is checked
independently using the current level ID: below `0x27` is campaign, `0x27` and
above is multiplayer. The lobby is multiplayer. Campaign ID -1 (the initial main menu) runs campaign callbacks. ID 0 and
other negative values defer loading/callbacks as pre-load states; the supplied pre-load multiplayer
menu dump has ID 0, so it must not dispatch campaign callbacks. A mismatch
between the level classification and SDK layout also defers callbacks.

| Current level global | NTSC-U | PAL | NTSC-J |
|---|---|---|---|
| Campaign | 0x001d545c | 0x001d545c | 0x001de75c |
| Multiplayer | 0x001f8528 | 0x001f83a8 | Unverified; defer callbacks |

Campaign evidence: Starship1/AnniNation memory dumps read 3/7 at 0x001d545c.
Two copies of the same AnniNation level check in each region's code.0005 file
load the regional global above; the verifier decodes their LUI/LW instructions.
Multiplayer evidence: every supplied NTSC-U/PAL dump reads its filename's level
ID (39 through 49) at the regional address. No region offset is guessed.

## Local Play teardown versus online profile selection

Live NTSC-U staging-exit trace: NetInfo.state at `0x1a5f70` remained 5 while
onlyLocalPlayers at `0x1a5fc0` became zero. Active UI slots contained local
create-game (ID 21) and local staging (ID 23). Treating these network states alone as online
incorrectly deactivated offline modules and freed their cached map catalog.

A second live trace after quitting a game showed state 10 with onlyLocalPlayers
clear and local create-game (ID 21) foremost.

With onlyLocalPlayers clear, context detection examines the foremost
nonzero active menu (slots `0x1c5c110` through `0x1c5c118`, last slot first).
Local IDs 21, 23, and multiplayer landing ID 24 remain local. Any other foremost
menu retains the network-state classification, including online profile ID 0;
a covered/stale Local Play menu cannot override the profile screen. UI pointer
table `0x1c5c064` and these IDs are shared by NTSC-U/PAL in libuya/ui.h. Native
regional tests cover this distinction; live PAL qualification remains pending.

## Loader and module discovery

The fixed loader discovery block occupies A0000-A0030. Its first word points
to the linker-placed module table; the table and persistent state follow the
payload in a NOLOAD section. Their addresses are not an ABI.
Modules start at `_loader_end` rounded up to 64 bytes and extend to 100000,
with the optional exception handler separately reserved at 98000-9E000. The PNACH generator gets hook targets
from `campaignEntry` and `multiplayerEntry`, now after the discovery block.

Version-4 modules begin with a 16-byte discovery header containing an optional
exports pointer. See README.md for layouts and the C declaration convention,
and modules/forge/PINE.md for the complete mailbox pointer chain.

### Expanded EE RAM

The loader calls BIOS syscall 0x7f (`GetMemorySize`) once at initialization.
PCSX2 overrides this syscall when extra RAM is enabled. A result of 08000000
selects the module pool 02000000-08000000 (exclusive end); otherwise the normal
aligned-loader-end pool is used. No speculative memory writes are used to
detect RAM. Handler, loader, discovery block, and module table stay in low RAM.

All supported regions build. Existing native checks pass, and 18 regional
module images match independently linked reference images at six placements,
including 02000000, 02018040, and 07F00000. PAL cold boot with normal RAM placed
modules at A3900; with expanded RAM it placed all seven installed modules at
02000000-0200C980. The sampled game stack remained at 01FFFB00. Full-pool
occupancy, campaign gameplay, and NTSC-U/NTSC-J expanded-RAM execution remain
untested; successful relocation checks do not prove those cases.

The first PAL multiplayer-entry test stopped in the BIOS syscall dispatcher
at 80000280 after the game's `_InitTLB` syscall stub at 0011F290 (syscall 82).
The selected BIOS was ps2-0120a-20000902.bin. This matches PCSX2 issue 11234:
old BIOS versions lack the syscall used by SDK InitTLB when GetMemorySize
reports expanded RAM. The module table and sampled module code were intact.
With the already-installed ps2-0230a-20080220.bin, the user confirmed entry to
Local Play staging. The existing Forge discovery chain found its mailbox at
0200C8A0. Select Aquatos Aquadome, Start Game, reload, hop to retail Bakisi Isles,
and hop back to Aquadome each completed with state 3/error 0 and the expected
level/context. The user confirmed gameplay, HUD, and Mod Menu opening and
navigation all work correctly. No game or
BIOS code workaround has been added.

The loader now also checks syscall 82 with the existing PS2SDK
GetSyscallHandler helper. With BIOS 1.20 and 128 MiB enabled, the check selected
the normal pool and set UYA_WARNING_EXTRA_RAM_BIOS. The optional bios-warning
module renders this flag through Mod Menu's module-owned modal callback;
the user confirmed the warning was visible. Initial dismissal testing exposed
an early-boot/movie timing issue. Automatic modals now wait until the boot
menu's idle counter becomes nonzero before capturing input. The user confirmed
the initial 30-frame wait allowed Cross dismissal and normal controls; the
wait is now shortened to the first idle tick. The user confirmed the popup
appears immediately after pressing Start to skip the intro/preview, with
working dismissal and restored controls.

## Relocated exception handler

The SM/Horizon 10,356-byte payload is relocated from C8000 to 98000.
Its 24 KiB reservation ends at 9E000, leaving 8 KiB before the loader at A0000.
PAL staging contains syscall/startup code and stack storage near 81DD0-83FFF.
Campaign dumps (Starship1 and Annihilation Nation) also contain code/data in
84000-95FFF. PAL cold boot confirmed 84000 was occupied and installation was
correctly skipped. The handler must not use either proposed 80000/84000 location,
and the loader must not use 90000. The inspected dumps are clear at 98000-A0000.

The original SHA-256 is `3c5685ab443a26216fef1b18cf549e437aa0d4586cd7ba0d869100155b854366`.
The relocated SHA-256 is `4929cf42067affb1b2e2610243e133c360a9c9a1b91cef3f885627098afd241b`.
59 internal JAL targets and 20 LUI/ORI address pairs shift by -30000 hex.
PC-relative branches, external kernel/GS addresses, strings, and font data are unchanged.
The original source reference is Horizon `misc/Exception_Display.cds`; its imported
CodeDesigner classes are not present locally. No new build tool is required.

The original drawing buffer at 90000 overlaps campaign memory. The existing
LUI s1,9 at binary offset 17C is retained; offset 184 now contains ORI s1,s1,C000
to select 9C000 in GetDisplay's previously empty delay slot. At offsets 470, 510,
and 520, text batching changes from 17 glyphs to one. Each glyph has at most
8 x 11 pixels with an 80-byte packet per pixel (7,040 bytes), fitting within
9C000-9E000. The payload and text scratch end before 9C000. The 9E000-A0000
gap is unused. PAL cold boot verified the installed bytes and all 11 kernel
exception vectors targeting 98088. A controlled fault reached that entry, saved
registers, and ran the renderer using 9C000. The loader payload and 9E000-A0000
gap remained unchanged. The user confirmed a readable exception/register screen.

## Loading-screen names during Forge hops

The staging name-builder hook does not cover gameplay overlays. On PAL Korgon
(base 43), a live reload breakpoint reached the corresponding builder return
at 541048. Its table at 34F500 contained retail names; the staging table at
352D20 was empty. The builder prologue constructs the table from its LUI s0 /
ADDIU fp,s0 pair, followed by ADDIU s0,fp,40 hex. These instructions and the
matching builder tails were checked in all 22 NTSC-U/PAL lobby/gameplay dumps.
The existing hopAddresses rows now include each builder return and name table.
Name-hook installation and cleanup use the active overlay's row, preserving
retail names when no custom override is selected. Native tests cover all ten
retail bases in both regions. PAL live testing completed an Aquatos Aquadome
start/reload, a hop to retail Bakisi Isles, and a hop back to Aquadome without
mailbox errors. During the reload, the active loading-name table contained
Aquatos Aquadome in the base-43 slot. NTSC-U has build/static coverage only.

The subsequent visual check still showed the retail name during a cross-base
hop. A breakpoint at the old Korgon builder (541048) proved LEVEL_ID already
held 40 while the Korgon overlay was still executing; nameTable had incorrectly
switched to Bakisi's 357640. Name-hook/table changes now wait for the same code
signature checks as the loading hooks. This keeps the source overlay's table
through the transition. Both regional builds and regression tests pass,
including a destination-ID-before-overlay case. PAL retesting started retail
Bakisi Isles and hopped to Aquatos Aquadome successfully. During that transition,
the source overlay's table at 357640 contained Aquatos Aquadome in slot 43.
The user confirmed the correct visible loading-screen name, working gameplay,
and HUD after the hop.

## PAL online test with the downloaded Horizon patch

At post-DNAS profile selection, context was ONLINE (4), all eight modules were
inactive, and the staging input, name-builder, and six map-loading hooks were
restored. After login, Horizon installed its return hook at 139E94 while the
loader entry hook remained at 139E08. The downloaded patch still used A0000
for its network callback pointer: live instructions at E2BA8, E2C3C, E2C54,
E2C60, and E2CC0 read/write that old slot. A0000 contained the loader table
pointer A35C0, while the new PAL callback slot 22F1A4 was zero. Execution
stopped at PC 1 with v1=A35C0 and v0=1, consistent with interpreting the module
table as callbacks. Online coexistence remains unverified until the deployed
patch and its separately linked networking consumers use the new callback slot.

## NTSC-U online follow-up (2026-10-23)

After restarting MCP PCSX2 in portable mode with current matching PNACHs and
modules, live reads confirmed the campaign entry hook and all eight module
placements. The user then tested online and reported that Horizon's patch loaded
without crashing. No new debugger capture of the downloaded patch or callback
slot was taken during that successful test. This confirms patch loading for
that setup, not every online game mode or module combination; the earlier PAL
stale-callback finding remains relevant to older patch builds.

## Horizon to Local Play map-loader handoff

NTSC-U live inspection after logout found all 65 maps cataloged, but Horizon
callbacks remained at `0x19507c`, `0x5a2560`, `0x5a2408`, and `0x5bf36c`.
The completed scan was hidden because the standalone loading group was not installed.
Horizon also retained its loading-name and level-exit hooks.

`modules/maploader/multiplayer.c` checks a 64-byte FNV-1a callback signature
against the local Horizon `patch-10683.bin` / `patch-10684.bin` builds before
reclaiming the complete group. NTSC-U callback bytes were matched against the
running patch; PAL signatures are from the regional binary and regression
fixtures, pending live transition verification. The handoff requires Local
Play staging, local loader context, and no lobby connection. Unknown builds or
partial hook groups are not overwritten. Restoration and installation run
together because Horizon can reinstall its hooks if retail calls are left
exposed between updates.

After installing the fix, the user verified that returning from Horizon online
play to Local Play staging shows the map list and that Square refreshes it.
This confirms the NTSC-U menu handoff and refresh path. PAL logout handoff and
starting a custom-map game after this handoff still need live verification.

## Forge boot-to-Local-Play handoff

The supplied NTSC-U code replaces the call at `0x00381590` (`0x0c0e8f8c`)
with `J 0x00398040`. Matching the boot overlay's instructions gives PAL's
corresponding call at `0x00381568` (`0x0c0e8fdc`) and target `0x00398128`.
Forge applies this jump from a campaign tick; retail executes it after the tick
returns, so an ELF switch cannot leave the loader's recursion guard set.

The target calls the native boot-settings encoder at `0x0013b0c8`. It encodes
eight settings bytes as sixteen hex characters in a `BOPT=` argument. Forge
keeps this payload unchanged. The target's final Exec wrapper call is at
`0x0039808c` (NTSC-U) / `0x00398174` (PAL), calling `0x00397fb8` / `0x003980a0`.
Forge wraps that call to append a separate `FORGE=<eight hex digits>` argument.
The multiplayer startup argument blocks are `0x001ebf80` / `0x001ebe00`, with
argc at +`0x00`, sixteen argv pointers at +`0x04`, and string storage at +`0x44`.
The received tag restores the boot route and PINE request ID after Forge reloads.
The native parsed-options byte at `0x001d551d` distinguishes a cold campaign boot
from a return carrying settings, preventing automatic redirection on return.

Once multiplayer is ready, Forge dispatches native select event 7 on Local Play
(entry 1) in landing menu 24. Its update functions are `0x006ae328` (NTSC-U) /
`0x006b0c90` (PAL). Local Create Game (menu 21) submits through `0x006a9f10` /
`0x006ac860`, then requests staging menu 23 via the native UI manager at
`0x00247a74` / `0x00247914`, next-menu field +`0x284`. These functions were
matched in the extracted regional lobby overlays. Completion waits for the
actual staging object; no Start Game callback is invoked.

### Optional custom-map quick loading

Forge exposes its saved `quickLoadMaps` toggle through `forgeApi_t` at +`0x0c`.
Map Loader bypasses five `JAL 0x00139e08` calls in the source overlay's fade and
transition loops, plus the resident TimeBandit store at `0x00195d84` (NTSC-U) /
`0x00195c94` (PAL). The regional `hopAddresses` rows include the first transition
and fade sites; the other calls are at +`0xa4` and +`0xd4`/`0x108`, respectively.
All five calls and their delay slots were matched in level 39–49 memory images.
The NTSC-U fade's second call is `0x00589560`, not the adjacent store at
`0x0058955c` used by the old standalone cheat.

Only Local Play custom-map loads opt in. Restoration tracks the source overlay
and owned instructions, so a newly loaded overlay is never patched with saved
instructions from the previous map. Normal timing returns after loading and
on deactivation. Retail loads and campaign are unchanged.

### Forge game modes

UYA's native Create Game mode selector calls `0x0019afd0` (NTSC-U) /
`0x0019ae90` (PAL). This resident routine copies 22 default rule bytes to the
local setup at `0x002417c8` / `0x00241648`, then applies Siege/CTF/Deathmatch
presets. Forge copies those rules to `GameSettings + 0x188` when changing modes,
sets team play for objective modes, and reapplies its two saved rule overrides.
The active mode is at `GameSettings + 0x180`; local setup's mode is at
`0x002407a8` / `0x00240628`. Staging uses child 2 (`menu + 0x118`) for its mode
label, as seen in the native NTSC-U staging builder at `0x006ab678`.

The appended `forgeApi_t.applyGameMode` callback at +`0x10` runs immediately
before Map Loader's Start Game or hop request. Menu changes during gameplay
only change the pending value; the current game keeps running its existing mode.
