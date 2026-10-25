# Forge PINE interface v2

Forge owns a **168-byte, 16-byte-aligned mailbox in its module BSS**.
There is no fixed mailbox address or loader-owned reservation.
Use PINE EE memory reads/writes; this is not a host-process pointer. Forge v2
needs no plugin-specific transport. The Forge mod consumes commands on its
normal game-thread update. No editor source changes are included here.

Enable `forge campaign,local`, `maploader local`, and optionally `modmenu` in modules.txt.
The protocol works without Mod Menu. Currently supported: NTSC-U and PAL Local Play
staging and supported in-match overlays 40-49. Command 1 can also enter Local Play from the boot menu. Online, loaded campaign levels,
and transitions do not accept new commands.

## Quick guide: send a command

First enable PINE in PCSX2, boot the game, and enter **Local Play staging** or
a running Local Play game (or the boot menu for command 1). Discover the mailbox using the pointer chain below;
never hard-code a module or mailbox address. In these examples, `M` means the
discovered mailbox address, and `id` is a fresh nonzero 32-bit request ID.

Every command uses this same procedure:

1. Verify the mailbox header and advancing heartbeat. Require `available != 0`
   at `M + 0x1c` and the expected context at `M + 0x1d`: `1` for staging,
   `2` for a running game, `3` for the boot menu. Wait for any previous command or menu-triggered
   scan/load to finish. Do not send during a transition.
2. Check the one-byte capabilities field at `M + 0x1e`:
   `(capabilities & (1 << command)) != 0` must be true.
3. Write the command number as **one byte** to `M + 0x1f`.
4. Optionally write the game mode as **one byte** to `M + 0x23`: `0` Siege,
   `1` CTF, or `2` Deathmatch. Omitted means Siege; see below.
   Write a **129-byte, zero-filled ASCII buffer** to `M + 0x24`, containing
   the filename or retail ID below. Leave it all zero for an empty argument.
   Finish all payload writes before the next step.
5. Write `id` as a **four-byte little-endian integer** to `M + 0x0c` **last**.
   This submits the request. Do not write any game-owned fields or overwrite
   the whole mailbox.
6. Poll for this exact `id`. If `rejectedId` at `M + 0x18` matches, stop and
   report `rejectError` at `M + 0x22`. Otherwise wait for `completedId` at
   `M + 0x14` to match. Success requires `state == 3` at `M + 0x20` and
   `error == 0` at `M + 0x21`. State `4` means failure; report `error`.
   `acceptedId == id` only means accepted, **not finished**.

Use one command at a time. Do not reset IDs to zero between commands. After
reconnecting, read the existing IDs and choose an unused ID for that session;
do not blindly start at `1`. If a command times out, report it and inspect the
heartbeat/context before retrying; it may still be running. Never automatically
resubmit a Start Game or hop with a new ID while its outcome is unknown.

### Enter Local Play from the boot menu

Enable Forge for `campaign,local`. With `bootToLocalPlay=0`, discover the mailbox
at the campaign boot menu and require context `3` with capability bit `0x02`.
Send command **1**, empty filename, writing the command to `M + 0x1f` and the
request ID to `M + 0x0c` last. Wait for successful completion: Local Play staging
is open, but no game has started. Then select a map and use command 6 if needed.

This request does not change the saved boot option. It survives the brief
unavailable context while the multiplayer ELF loads. Do not resend it during
that transition. Map Loader and Mod Menu are not required for command 1.

### Refresh the custom map list

1. Finish copying/building all map files into `uya/` before requesting a scan.
2. In staging or a running game, send **command `2`**, empty argument.
3. Wait for successful completion. The catalog is now refreshed.

Refreshing does **not** reload the current level or start a game. If map content
changed and you want to load it immediately, use command `4` below instead.

### Hop to a different custom map while playing

1. Require context `2` (already in a Local Play game).
2. Send **command `3`** with the map's filename stem, for example `minikisi`.
   If it was just added or rebuilt, send **command `4`** instead: it refreshes
   the catalog and then loads that map in one request.
3. Wait for successful completion. The new map is playable in the existing
   game; no return to staging or Start Game command is needed.

Use the stem from `uya/minikisi.version`: `minikisi`, not the display name
`Bakisi Mini`, not `minikisi.wad`, and not a full path. Use the same stem for
PAL; do not append `.pal`. The loader chooses the regional assets.

### Choose a custom map and start a new game from staging

1. Require context `1` (Local Play staging).
2. Send **command `3`**, argument `minikisi`, or **command `4`** if the map
   was added/rebuilt and needs a refresh first.
3. Wait for **successful completion**. The map is selected and its staging
   name is updated, but the game has not started.
4. Send **command `6`**, empty argument, using a **new request ID**.
5. Wait for successful completion. Gameplay is ready on the selected map.

Stop if selection fails; sending Start Game anyway would use the previous
selection. To start whatever map is already selected, skip steps 2 and 3.
Command `6` does not accept a map filename. Its mode byte chooses the game mode.

### Hop to a retail map or reload the current map

- **Retail hop:** in a running game, send **command `5`**, argument `"43"`
  for Korgon Outpost, and wait for success. The argument is ASCII decimal text,
  not a binary integer. Supported retail IDs are listed below.
- **Retail start:** in staging, send command `5`, wait for success, then send
  command `6` with an empty argument and a new ID.
- **Reload:** in a running game, send **command `7`**, empty argument, and
  wait for success. It reloads the current retail/custom map. For a rebuilt
  custom map whose metadata or file sizes changed, use command `4` with its
  filename instead so the catalog is refreshed before loading.

### Choose the game mode

Commands **3, 4, 5, 6, and 7** accept the optional byte at `M + 0x23`.
This selects a mode for the existing command, not a separate command number:

| Value | Mode |
| --- | --- |
| 0 | Siege (default) |
| 1 | CTF |
| 2 | Deathmatch |

Write the byte before publishing the request ID. Accepted requests reset it to
zero, so omitting it on the next request selects Siege. Client helpers should
explicitly write zero when their optional mode argument is omitted, including
a retry after rejection. Values above 2 are rejected with error 9.
Refresh (2) and Enter Local Play (1) ignore this byte.

In staging, selection commands also update the game mode and its UI label.
Start Game (6) uses its own mode byte: to select a map and then start CTF, write
`1` for **both** requests. During gameplay, a select/hop or reload applies the
requested mode with UYA's native objective/team defaults. The map must contain
the objectives needed by that mode. Requests do not automatically save settings.

Examples:

`sendAndWait(command = 7, filename = "", gameMode = 2)` reloads in Deathmatch.
The next `sendAndWait(command = 7, filename = "")` reloads in Siege.
`sendAndWait(command = 5, filename = "40", gameMode = 1)` hops to Bakisi Isles in CTF.

### Editor integration example

This is pseudocode, not a new script or a PINE library API. `sendAndWait` means
the six-step submission/completion procedure above and returns only on success;
on rejection, failure, or timeout it stops the sequence and reports the result.

```text
refresh button:
	sendAndWait(command = 2, filename = "", id = nextRequestId())

test newly built custom map:
	wait until all build output files are completely written
	if context == 1:                         // staging
		sendAndWait(command = 4, filename = "minikisi", id = nextRequestId())
		sendAndWait(command = 6, filename = "", id = nextRequestId())
	else if context == 2:                    // running game
		sendAndWait(command = 4, filename = "minikisi", id = nextRequestId())
	else:
		report "Enter Local Play staging or a running Local Play game"
```

Recheck availability/context/capabilities before each submission. Keep the
emulator running while waiting, and rediscover the mailbox after a restart or
savestate load. Numeric states/errors and the full protocol are documented below.

## Layout

Fields use the widths listed below; multibyte integers are little-endian. Strings are NUL-terminated ASCII.
Offsets are relative to the discovered mailbox address. The C declaration is
`modules/forge/include/forge.h`; the ELF symbol is `forgeMailbox`.

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
   +`0x0c`. The C declaration is `forgeExports_t` in `modules/forge/include/forge.h`.
5. Require a 16-byte-aligned mailbox entirely within Forge's address/size range.
   Check mailbox magic `0x32475246` (`FRG2`), 16-bit version `2`, and 16-bit size `168`.
   Re-read discovery pointers and the table entry to detect a restart during
   discovery. Verify heartbeat at mailbox + `0x08` advances, then check
   availability, context and capability bits before submitting work.

The headers and exports table are read-only to the host. The exports and mailbox
pointers exist as soon as Forge is loaded, but the mailbox header is initialized
only when Forge first initializes in an allowed context. Loaded does not imply
active. If the header is not ready or the heartbeat is stopped, wait for the
boot menu or running Local Play. A paused emulator also stops the heartbeat.

Cache addresses only for the current session. Rediscover after restart,
savestate load, module replacement, changed header, or stopped heartbeat.

**Protocol v2 is incompatible with v1.** Rebuild/deploy Forge, Map Loader, and
`mods/utils/mapscan.irx` together and cold-boot. Update external clients before
sending commands. The loader and exports discovery headers remain version 1;
the mailbox uses a new magic and version. Forge/Map Loader service ABI is now 2
so mismatched modules cannot silently truncate filenames.

The 48-byte advisory message was removed. Display status from `state`, `error`,
and `rejectError`. Command IDs follow the table below. IDs and
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
| 0x1c | 1 | available | Game: Map Loader or boot backend available |
| 0x1d | 1 | context | Game: 0 unavailable, 1 staging, 2 game, 3 boot menu |
| 0x1e | 1 | capabilities | Game: bit `(1 << command)` |
| 0x1f | 1 | command | Host: command number |
| 0x20 | 1 | state | Game: accepted operation state |
| 0x21 | 1 | error | Game: accepted operation error |
| 0x22 | 1 | rejectError | Game: rejected operation error |
| 0x23 | 1 | gameMode | Host: 0 Siege (default), 1 CTF, 2 Deathmatch |
| 0x24 | 129 | filename | Host: up to 128 ASCII characters plus NUL |
| 0xa5 | 3 | padding | Zero; do not write |

Write `command` as **one byte**, not a 32-bit word: adjacent bytes belong to the
game. Write filename separately, then the aligned 32-bit request ID last.

## Commands

| Number | Command | Staging | In match |
|---|---|---|---|
| 1 | Enter Local Play | Boot menu only; enters staging | Reject |
| 2 | Refresh | Rescan catalog | Rescan catalog |
| 3 | Select/load custom map | Select custom map | Hop to custom map |
| 4 | Refresh and select/load custom map | Rescan and select custom map | Rescan and hop to custom map |
| 5 | Select/load retail map | Clear custom override and select retail base | Hop to retail base |
| 6 | Start Game | Start with the selected map/settings | Reject |
| 7 | Reload current map | Reject | Reload the current retail/custom map |

Start Game takes no filename. Select a map with command 3 or 4 first if needed,
wait for that request to finish, then submit command 6 with a new ID. Retail maps
also work: whatever is selected in staging is used. The command defers the normal
retail Start Game callback until the next update, allowing Mod Menu to close,
and reports completion only when gameplay is ready. It preserves the game's
normal roster, settings, overlay selection and HUD initialization. The callback
is signature-checked; unavailable code or invalid selected map files reject the
request. Leaving staging before execution cancels it. It cannot start online games.
Capabilities are `0x02` at boot (command 1 only), `0x7c` in staging, and `0xbc` in game.

Command 5 (`FORGE_SELECT_RETAIL`) uses the existing filename field as a
NUL-terminated **decimal base-map ID**, for example `"43"`. Exactly `40` through
`49` are accepted. It takes no host path and does not scan custom maps. Check
capability bit `(1 << 5)` before sending it. In staging, wait for completion and
then send command 6 if you want to start the game. In gameplay, completion means
the retail transition and HUD setup finished. Command 7 subsequently reloads that
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
catalog only: use command 7 or 4 to load changed map content.

## Submission and completion

1. Read magic/version/size, available, context and capabilities. Verify heartbeat
   advances when emulation is running. A stopped heartbeat may simply mean pause.
2. Wait for earlier accepted work to complete. Choose a fresh nonzero request ID;
   never reuse an ID in a session (skip zero on wraparound).
3. Write command, optional gameMode (zero for Siege), and a zero-padded 129-byte
   filename field. For commands 1/2/6/7, an empty filename is sufficient.
   Wait for these PINE writes to complete.
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
5 map not found, 6 scan failure, 7 selection/loading/HUD failure, 8 inactive, 9 invalid game mode.
Rejected requests have separate ID/error fields and do not replace the accepted operation.

Leaving Local Play cancels pending map responses. Command 1 carries its request
ID through the native ELF argument handoff and resumes in multiplayer. The
loader may reload Forge and reset its heartbeat during this operation;
rediscover its mailbox while waiting, retaining the same request ID.
Requests posted while inactive are discarded on reactivation. Restarting the
game resets the mailbox; reconnect and recheck the handshake. Savestate rewind
can rewind IDs and heartbeat, so the editor should abandon outstanding requests
and establish a new session after loading a savestate. No network authentication
is implemented here; access control belongs to the emulator's PINE endpoint.

## Verification

Protocol-v2 layout, 128-character names, and unterminated-name rejection are
covered by native checks, along with explicit/default mode values and invalid
mode rejection. Direct PINE tests on PAL selected Bakisi Isles in CTF, started
the game in CTF, and reloaded without writing the mode byte; that request
submitted zero and completed in Siege with its native rules restored.

Native tests cover request ordering, duplicate IDs, busy rejection, inactivity,
invalid filenames, context restrictions, and scan failures. Live NTSC-U checks
covered refresh, selection, Start Game, custom/retail hops, and repeated reloads
with working gameplay and HUD. PAL covered Bakisi Isles and Aquatos Aquadome
starts, reloads, and hops, including the custom loading-screen name fix.

Earlier map-transition tests used MCP debugger reads/writes to the EE mailbox;
the mode tests above used PCSX2's PINE endpoint. Forge v2 editor integration
still needs verification. Temple of Shaar's PAL loading
failure remains unresolved; campaign, online, NTSC-J, and split-screen hopping
are unsupported. See [Forge support notes](README.md#support-and-limitations).
