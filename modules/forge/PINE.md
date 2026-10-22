# Forge PINE interface v1

Forge owns a **172-byte, 16-byte-aligned mailbox in its module BSS**.
There is no fixed mailbox address or loader-owned reservation.
Use PINE EE memory reads/writes; this is not a host-process pointer. Forge v2
needs no plugin-specific transport. The Forge mod consumes commands on its
normal game-thread update. No editor source changes are included here.

Enable `forge local`, `maploader`, and optionally `modmenu` in modules.txt.
The protocol works without Mod Menu. Currently supported: NTSC-U and PAL Local Play
staging and supported in-match overlays 40-49. Online, campaign, boot menus,
and transitions do not accept new map commands.

## Layout

All integers are unsigned 32-bit little-endian. Strings are NUL-terminated ASCII.
Offsets are relative to the discovered mailbox address. The C declaration is
`include/forge.h`; the ELF symbol is `forgeMailbox`.

### Discovery through the loader and module headers

PINE follows **0xA0000 -> module table -> Forge -> exports -> mailbox**.
No memory scan is needed. All pointers are 32-bit little-endian EE addresses.

1. At `0x000A0000`, verify loader magic `0x444C5955` at +`0x04`, discovery
   version `1` at +`0x08`, and size `48` at +`0x0C`. Read the module-table
   pointer from +`0x00`. Its location is linker-selected, not fixed at AF800.
2. Read the table header: magic `0x554D5442`, version `1`, count (at most 8),
   and entrySize `124`. Wait if the loader has not initialized it yet.
   Entries begin at table + `0x10`. Find `forge.ntscu.bin` or `forge.pal.bin` in the 112-byte
   NUL-terminated name. Address, size and modes are at entry +`0x70`, +`0x74`,
   and +`0x78`. Check the table is inside the loader's A0000-B0000 reservation
   and the full module range is inside EE memory before following it.
3. At Forge's base, verify module magic `0x444D5955` (`UYMD`) at +`0x04`,
   version `1` at +`0x08`, and size `16` at +`0x0C`. Read the exports
   pointer from +`0x00`. Require a nonzero, four-byte-aligned pointer and a
   complete 16-byte exports structure inside Forge's address/size range.
4. Forge exports contain magic `0x31584746` (`FGX1`) at +`0x00`, version
   `1` at +`0x04`, size `16` at +`0x08`, and the mailbox pointer at
   +`0x0C`. The C declaration is `ForgeExports` in `include/forge.h`.
5. Require a 16-byte-aligned mailbox entirely within Forge's address/size range.
   Check mailbox magic `0x31475246` (`FRG1`), version `1`, and size `172`.
   Re-read discovery pointers and the table entry to detect a restart during
   discovery. Verify heartbeat at mailbox + `0x18` advances, then check
   availability, context and capability bits before submitting work.

The headers and exports table are read-only to the host. The exports and mailbox
pointers exist as soon as Forge is loaded, but the mailbox header is initialized
only when Forge first initializes in Local Play. Loaded does not imply active.
If the header is not ready or the heartbeat is stopped, wait for running Local
Play instead of writing commands. A paused emulator also stops the heartbeat.

Cache addresses only for the current session. Rediscover after restart,
savestate load, module reload, changed header, or stopped heartbeat. This
replaces both the fixed AF800 table location and Forge's former FGP1 entry stub.
Do not use the old base + 8 mailbox lookup or fall back to `0x000FFF00`.
Rebuild and deploy the version-4 loader and modules together. The mailbox's
command numbers and field offsets below remain unchanged.

| Offset | Field | Owner / meaning |
|---|---|---|
| 0x00 | magic | Game: `0x31475246` (`FRG1`) |
| 0x04 | version | Game: 1 |
| 0x08 | size | Game: 172 bytes |
| 0x0C | available | Game: Forge and compatible Map Loader active |
| 0x10 | context | Game: 0 unavailable, 1 staging, 2 match |
| 0x14 | capabilities | Game: bit `(1 << command)` indicates context support |
| 0x18 | heartbeat | Game: advances on Forge updates; not a frame clock |
| 0x1C | requestId | **Host: write LAST to submit**, nonzero |
| 0x20 | command | **Host**: command number below |
| 0x24 | filename[64] | **Host**: map-pack filename stem, not display name |
| 0x64 | acceptedId | Game: most recently accepted request |
| 0x68 | completedId | Game: most recently finished accepted request |
| 0x6C | state | Game: state of accepted work |
| 0x70 | error | Game: terminal error for accepted work |
| 0x74 | rejectedId | Game: most recently rejected request |
| 0x78 | rejectError | Game: rejection reason |
| 0x7C | message[48] | Game: advisory progress text; use numeric fields for logic |

## Commands

| Number | Command | Staging | In match |
|---|---|---|---|
| 1 | Reload current | Reject: no active game | Reload the current retail or custom map |
| 2 | Refresh | Rescan catalog | Rescan catalog |
| 3 | Select/load filename | Select map and update staging name; user still presses Start | Hop to map |
| 4 | Refresh and select/load filename | Rescan, find and select map | Rescan, find and hop to map |
| 5 | Start Game | Start with the current map and settings | Reject: already in a game |
| 6 | Select/load retail map | Clear custom override and select retail base | Hop to retail base |

Start Game takes no filename. Select a map with command 3 or 4 first if needed,
wait for that request to finish, then submit command 5 with a new ID. Retail maps
also work: whatever is selected in staging is used. The command defers the normal
retail Start Game callback until the next update, allowing Mod Menu to close,
and reports completion only when gameplay is ready. It preserves the game's
normal roster, settings, overlay selection and HUD initialization. The callback
is signature-checked; unavailable code or invalid selected map files reject the
request. Leaving staging before execution cancels it. It cannot start online games.
Staging capabilities are now `0x7C`; in-game capabilities are `0x5E`.
Older builds without bit `(1 << 5)` do not implement Start Game.

Command 6 (`FORGE_SELECT_RETAIL`) uses the existing filename field as a
NUL-terminated **decimal base-map ID**, for example `"43"`. Exactly `40` through
`49` are accepted. It takes no host path and does not scan custom maps. Check
capability bit `(1 << 6)` before sending it. In staging, wait for completion and
then send command 5 if you want to start the game. In gameplay, completion means
the retail transition and HUD setup finished. Command 1 subsequently reloads that
retail map; commands 3/4 can switch back to custom maps.

| ID | Retail map |
|---|---|
| 40 | Bakisi Isles |
| 41 | Hoven Gorge |
| 42 | Outpost X12 |
| 43 | Korgon Outpost |
| 44 | Metropolis |
| 45 | Blackwater City |
| 46 | Command Center |
| 47 | Blackwater Docks |
| 48 | Aquatos Sewers |
| 49 | Marcadia Palace |

Example filename: `minikisi`, not `Bakisi Mini`, `minikisi.wad`, or a host path.
Names are case-sensitive, 1-63 printable ASCII bytes. Separators, traversal,
colons, wildcards, leading dots and unterminated strings are rejected. Files must
pass Map Loader's usual map-pack validation. A missing map is an explicit error.
Refresh preserves selection by filename when present; removed maps lose selection.
In-game refresh requires the same mapscan helper as staging. It updates the
catalog only: use command 1 or 4 to load changed map content.

## Submission and completion

1. Read magic/version/size, available, context and capabilities. Verify heartbeat
   advances when emulation is running. A stopped heartbeat may simply mean pause.
2. Wait for earlier accepted work to complete. Choose a fresh nonzero request ID;
   never reuse an ID in a session (skip zero on wraparound).
3. Write command and a zero-padded 64-byte filename field. For commands 1/2/5,
   an empty filename is sufficient. Wait for these PINE writes to complete.
4. Write requestId **last**, in a separate completed write.
5. Poll: `rejectedId == id` reports rejection; `acceptedId == id` acknowledges
   work; `completedId == id` reports terminal state/error. Do not interpret an
   old completedId or old rejection as the current response.

The mod copies the payload and rechecks requestId before accepting it. Do not
modify payload fields until acknowledged, and use one editor/writer per mailbox.
Repeating the same requestId does not execute it again. A new request while busy
is rejected without replacing active work; its rejection has separate fields.
If accepted, IDs/status are written before the completion marker. Read the
completion marker again around status reads if submitting consecutive requests.

States: 0 idle, 1 scanning, 2 loading/queued transition, 3 complete, 4 failed.
Errors: 0 none, 1 wrong context, 2 busy, 3 unknown command, 4 invalid filename,
5 map not found, 6 scan failure, 7 selection/loading/HUD failure, 8 inactive.
The text field is advisory and may reflect the previous operation on rejection.

Leaving Local Play marks Forge unavailable and cancels its pending response.
Requests posted while inactive are discarded on reactivation. Restarting the
game resets the mailbox; reconnect and recheck the handshake. Savestate rewind
can rewind IDs and heartbeat, so the editor should abandon outstanding requests
and establish a new session after loading a savestate. No network authentication
is implemented here; access control belongs to the emulator's PINE endpoint.

## Verification

Native tests cover commit ordering, duplicate IDs, busy rejection, inactivity,
malformed filenames, staging-only selection, in-match hopping, and scan failure.
Live NTSC-U Local Play staging checks passed using debugger writes to the same
EE mailbox: refresh, select Bakisi Mini, refresh-and-select Temple of Shaar,
and rejection of reload while no match is running. Busy requests were rejected
without interrupting the active refresh. In-match mailbox reload of Temple of
Shaar, refresh-and-hop to Bakisi Mini, and a subsequent Bakisi Mini reload all
reported success and returned game/scene flags to ready. The user confirmed
playable gameplay with the HUD visible after the sequence. These tests used the
MCP debugger memory interface; editor-side PINE transport still needs integration
verification.

Start Game: native tests and all regional builds pass. In live NTSC-U Local Play,
command 5 started the selected Temple of Shaar map, reported successful completion,
and returned game/scene flags to ready. A second Start Game request during gameplay
was rejected with wrong-context error 1. The user confirmed gameplay and HUD
work normally.

Command 6 live checks: Temple of Shaar -> retail Korgon Outpost completed and
cleared custom asset overrides. The user confirmed the stock loading name and
HUD. Command 1 then reloaded Korgon with overrides still disabled; command 3
returned to Bakisi Mini with custom loading re-enabled. Both reported success
and returned to gameplay. The user confirmed normal gameplay and HUD after
the sequence.

PAL uses the same commands and discovery layout. Regional routines are matched
to game dumps. Live Start Game and reload completed on retail Bakisi Isles and
Aquatos Aquadome. Aquadome -> retail Bakisi -> Aquadome completed with successful
HUD setup. The user confirmed gameplay, HUD, and the Forge menu reload action.
Temple of Shaar crashes during normal PAL starts as well as hops;
that map-specific loading case remains unresolved.
