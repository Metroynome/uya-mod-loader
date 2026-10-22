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
| template / Horizon return hook | 0x0012c994 | 0x00139e94 |

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

The exact return/delay-slot instructions and SDK entry signatures were verified
against those source binaries; the verified tables are checked in as C. Runtime checks refuse an
unexpected SDK entry layout before file operations. Hook patches require the
surrounding `ld s0, 0(sp)` and `addiu sp, sp, 32` instructions to match.
`entry.S` preserves v0/v1, ra, and gp; it leaves the original delay slot intact.

Campaign uses the `campaign/printf/template` hook at 0x0012c994 in all three
regions. Multiplayer uses Horizon's 0x00139e94 hook for NTSC-U and PAL.
These are matching return sequences in different resident layouts. The campaign
EE snapshots already have the printf template jump at their hook; extracted
boot cores retain the original return. NTSC-J emits only the campaign hook.

Remaining verification: cold boots, callback cadence, host0 availability, level
changes, and campaign/multiplayer transitions in PCSX2 for each intended disc.

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
