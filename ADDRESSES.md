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

`tools/verify_addresses.py` checks the exact return/delay-slot instructions and
SDK entry signatures against those source binaries. Runtime checks refuse an
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
