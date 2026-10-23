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
NTSC-U/NTSC-J live testing and full Horizon coexistence remain unverified.

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

The loader calls BIOS syscall 0x7F (`GetMemorySize`) once at initialization.
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
