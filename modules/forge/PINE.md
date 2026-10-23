# Forge PINE interface v2

Forge owns a **168-byte, 16-byte-aligned mailbox in its module BSS**.
There is no fixed mailbox address or loader-owned reservation.
Use PINE EE memory reads/writes; this is not a host-process pointer. Forge v2
needs no plugin-specific transport. The Forge mod consumes commands on its
normal game-thread update. No editor source changes are included here.

Enable `forge local`, `maploader`, and optionally `modmenu` in modules.txt.
The protocol works without Mod Menu. Currently supported: NTSC-U and PAL Local Play
staging and supported in-match overlays 40-49. Online, campaign, boot menus,
and transitions do not accept new map commands.

## Layout

Fields use the widths listed below; multibyte integers are little-endian. Strings are NUL-terminated ASCII.
Offsets are relative to the discovered mailbox address. The C declaration is
`include/forge.h`; the ELF symbol is `forgeMailbox`.

### Discovery through the loader and module headers

PINE follows **0xa0000 -> module table -> Forge -> exports -> mailbox**.
No memory scan is needed. All pointers are 32-bit little-endian EE addresses.

1. At `0x000a0000`, verify loader magic `0x444c5955` at +`0x04`, discovery
   version `1` at +`0x08`, and size `48` at +`0x0c`. Read the module-table
   pointer from +`0x00`. Its location is linker-selected, not fixed at AF800.
2. Read the table header: magic `0x554d5442`, version `1`, count (at most 8),
   and entrySize `124`. Wait if the loader has not initialized it yet.
   Entries begin at table + `0x10`. Find `forge.ntscu.bin` or `forge.pal.bin` in the 112-byte
   NUL-terminated name. Address, size and modes are at entry +`0x70`, +`0x74`,
   and +`0x78`. Check the table is inside the loader/module A0000-100000 range
   and the full module range is inside EE memory before following it.
   With PCSX2's 128 MiB RAM enabled, modules can be in
   `0x02000000-0x07ffffff`; the loader/table remain in low memory.
   Do not reject Forge solely because its address is above the normal 32 MiB.
3. At Forge's base, verify module magic `0x444d5955` (`UYMD`) at +`0x04`,
   version `1` at +`0x08`, and size `16` at +`0x0c`. Read the exports
   pointer from +`0x00`. Require a nonzero, four-byte-aligned pointer and a
   complete 16-byte exports structure inside Forge's address/size range.
4. Forge exports contain magic `0x31584746` (`FGX1`) at +`0x00`, version
   `1` at +`0x04`, size `16` at +`0x08`, and the mailbox pointer at
   +`0x0c`. The C declaration is `forgeExports_t` in `include/forge.h`.
5. Require a 16-byte-aligned mailbox entirely within Forge's address/size range.
   Check mailbox magic `0x32475246` (`FRG2`), 16-bit version `2`, and 16-bit size `168`.
   Re-read discovery pointers and the table entry to detect a restart during
   discovery. Verify heartbeat at mailbox + `0x08` advances, then check
   availability, context and capability bits before submitting work.

The headers and exports table are read-only to the host. The exports and mailbox
pointers exist as soon as Forge is loaded, but the mailbox header is initialized
only when Forge first initializes in Local Play. Loaded does not imply active.
If the header is not ready or the heartbeat is stopped, wait for running Local
Play instead of writing commands. A paused emulator also stops the heartbeat.

Cache addresses only for the current session. Rediscover after restart,
savestate load, module replacement, changed header, or stopped heartbeat.

**Protocol v2 is incompatible with v1.** Rebuild/deploy Forge, Map Loader, and
`mods/utils/mapscan.irx` together and cold-boot. Update external clients before
sending commands. The loader and exports discovery headers remain version 1;
the mailbox uses a new magic and version. Forge/Map Loader service ABI is now 2
so mismatched modules cannot silently truncate filenames.

The 48-byte advisory message was removed. Display status from `state`, `error`,
and `rejectError`; command numbers and numeric results are unchanged. IDs and
heartbeat remain 32-bit to avoid short-counter rollover during editor sessions.
The mailbox is 168 bytes (was 172) while accepting 128 filename characters.

| Offset | Bytes | Field | Owner / meaning |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | Game: `0x32475246` (`FRG2`) |
| 0x04 | 2 | version | Game: 2 |
| 0x06 | 2 | size | Game: 168 |
| 0x08 | 4 | heartbeat | Game: advances on Forge updates |
| 0x0c | 4 | requestId | **Host: write LAST**, nonzero |
| 0x10 | 4 | acceptedId | Game: most recently accepted request |
| 0x14 | 4 | completedId | Game: most recently completed request |
| 0x18 | 4 | rejectedId | Game: most recently rejected request |
| 0x1c | 1 | available | Game: compatible Map Loader available |
| 0x1d | 1 | context | Game: 0 unavailable, 1 staging, 2 game |
| 0x1e | 1 | capabilities | Game: bit `(1 << command)` |
| 0x1f | 1 | command | Host: command number |
| 0x20 | 1 | state | Game: accepted operation state |
| 0x21 | 1 | error | Game: accepted operation error |
| 0x22 | 1 | rejectError | Game: rejected operation error |
| 0x23 | 1 | reserved | Zero; do not write |
| 0x24 | 129 | filename | Host: up to 128 ASCII characters plus NUL |
| 0xa5 | 3 | padding | Zero; do not write |

Write `command` as **one byte**, not a 32-bit word: adjacent bytes belong to the
game. Write filename separately, then the aligned 32-bit request ID last.

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
Staging capabilities are now `0x7c`; in-game capabilities are `0x5e`.
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
Names are case-sensitive, 1-128 printable ASCII bytes. Separators, traversal,
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
3. Write command and a zero-padded 129-byte filename field. For commands 1/2/5,
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
Rejected requests have separate ID/error fields and do not replace the accepted operation.

Leaving Local Play marks Forge unavailable and cancels its pending response.
Requests posted while inactive are discarded on reactivation. Restarting the
game resets the mailbox; reconnect and recheck the handshake. Savestate rewind
can rewind IDs and heartbeat, so the editor should abandon outstanding requests
and establish a new session after loading a savestate. No network authentication
is implemented here; access control belongs to the emulator's PINE endpoint.

## Verification

Protocol-v2 layout, 128-character names, and unterminated-name rejection are
covered by native checks. The live gameplay results below are from protocol v1;
v2 has not yet been exercised through PCSX2/PINE.


Native tests cover request ordering, duplicate IDs, busy rejection, inactivity,
invalid filenames, context restrictions, and scan failures. Live NTSC-U checks
covered refresh, selection, Start Game, custom/retail hops, and repeated reloads
with working gameplay and HUD. PAL covered Bakisi Isles and Aquatos Aquadome
starts, reloads, and hops, including the custom loading-screen name fix.

These tests used MCP debugger reads/writes to the EE mailbox. Editor-side PINE
transport still needs integration verification. Temple of Shaar's PAL loading
failure remains unresolved; campaign, online, NTSC-J, and split-screen hopping
are unsupported. See [Forge support notes](README.md#support-and-limitations).
