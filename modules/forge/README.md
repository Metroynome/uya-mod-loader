# Forge mod suggestions

Status: NTSC-U and PAL Local Play map hopping are implemented. NTSC-U same-map
reloads and a cross-base hop followed by repeated reloads have passed live testing. The remaining sections retain the planned feature suggestions;
they are not a claim that all features exist. No Forge v2 source changes are included. The PINE mailbox is documented in
[PINE.md](PINE.md), including the current live verification coverage.

## Current prototype

Enable `forge local` alongside `maploader` and `modmenu` in modules.txt.
Rebuild and deploy all modules together, then cold-boot. Open MOD MENU from the
pause menu. Forge provides **Reload Current Map** for retail or custom maps, and the Custom Maps list can
request a hop during gameplay while Forge is active. In-game `None` is rejected;
its staging behavior is unchanged. This prototype supports NTSC-U and PAL retail base
overlays 40-49; split-screen overlays 50-55 and NTSC-J hopping remain disabled.

Requests are deferred to Map Loader, validated against the installed map files,
and sent through a signature-checked retail scene-change routine. Acceptance is
not completion. Repeated/busy requests are rejected. Automatic build detection,
shortcut controls, position restoration, and editor-side PINE integration
are still future work. Refresh Custom Maps and the PINE command mailbox are implemented, including
**Start Game** from Local Play staging using the selected map and game settings. Live coverage is limited to the scenarios listed below;
other map bases and player counts still need testing before a general release.


## Live verification

- NTSC-U, offline Local Play, Temple of Shaar (Korgon base 43):
  **Reload Current Map** returned to playable gameplay. The debugger confirmed
  the hop completed, game/scene flags returned to ready, and the local player's
  network data was recreated.
- Temple of Shaar -> Bakisi Mini (Bakisi base 40): reached playable gameplay
  without returning to staging, with HUD visible after the destination HUD setup
  fix. The debugger confirmed the game/scene ready flags and HUD registrations.
- Temple of Shaar -> Bakisi Mini followed by two consecutive Bakisi Mini
  reloads: user confirmed the HUD appears in all tested scenarios. HUD setup
  runs once after every completed Forge transition, including same-base reloads.
  Sequential-hop regression tests also pass.
- Mailbox commands: staging refresh/select and refresh-and-select passed;
  in-match Temple of Shaar reload, refresh-and-hop to Bakisi Mini, and Bakisi
  Mini reload returned successful completion and game/scene ready flags.
  The user confirmed gameplay and HUD were working after the sequence.
  See [PINE.md](PINE.md) for protocol coverage and remaining verification.
- **Start Game** mailbox command: started the selected Temple of Shaar directly
  from staging; user confirmed gameplay and HUD work normally. A second request
  during gameplay was rejected. Retail-map and split-screen starts remain untested.
- PAL: loading calls, scene requests, HUD setup, and Start Game matched against
  regional memory dumps. Live mailbox Start Game and reload completed on retail
  Bakisi Isles and Aquatos Aquadome. Aquadome -> retail Bakisi -> Aquadome
  completed with successful HUD setup. The user confirmed gameplay, HUD, and
  Forge menu Reload Current Map work on PAL. Temple of Shaar crashed both when started
  normally from staging and when reached by a hop; its PAL loading case remains
  unresolved. NTSC-J and split-screen hopping remain disabled.

The reload resets transient Local Play network-player records at the level TOC
loading boundary. This is necessary because the retail local-player constructor
rejects an existing player-data pointer and otherwise leaves its output NULL.
The reset runs once per active offline hop; match settings remain in place.

## Scope agreed with the user

- Create a separate UYA Mod Loader mod named `forge`.
- Do not change Horizon Forge v2. Its developer will handle editor integration.
- Implement UYA's equivalent of Deadlocked's `mapHopTo` behavior: reload the
  current custom map or hop to another custom map without manually leaving the
  match, returning to staging, and creating a new match.
- Allow access to the Custom Maps list during gameplay only while the Forge mod
  is enabled and active. Normal Local Play staging selection remains available
  through Map Loader without Forge.
- Detect newly added maps and updated map versions automatically where possible.
- Add a **Refresh Custom Maps** action to Mod Menu, available in Local Play staging
  and, while Forge is active, during gameplay.
- Assume the PCSX2 instance used by Forge v2 has PINE enabled. Treat this as an
  integration opportunity, not a requirement for manual use of the mod.

## Existing reference behavior

Deadlocked's patch uses **L1 + Up + Circle** when its Reload Custom Map setting is
on and a custom map is selected. It calls `mapHopTo()` for that map. The routine
resets map-loading and player readiness state, handles transition resources, and
invokes the game's level loader. It is not just a file reread.

References:

- `Horizon/horizon-deadlocked-patch/patch/main.c`: reload shortcut.
- `Horizon/horizon-deadlocked-patch/patch/config.c`: Reload Custom Map setting.
- `Horizon/horizon-deadlocked-patch/patch/maploader.c`: `mapHopTo` and transition setup.
- [Forge v2](https://github.com/Horizon-Private-Server/horizon-forge-v2), reviewed at
  commit `4a81e18`: its existing build pipeline packs a level and patches a
  development ISO. A compatible hostfs export remains work for its developer.

UYA needs its own verified transition code and regional addresses. Do not copy
Deadlocked addresses or assume its network/player structures match Local Play.
A hop should retain the session and relevant match settings; exact treatment of
scores, timers, player state, and respawning must be established through testing.
This is a level transition with a loading screen, not seamless live geometry editing.

## Responsibilities and API boundary

**Map Loader** should own catalog discovery, validation, selected/active map
identity, asset reads, and the game-specific reload/hop implementation. Expose a
small versioned service through the loader's existing service mechanism:

- Query capabilities, current map, catalog revision, and operation status.
- Request a catalog refresh.
- Request a reload of the active map's newest complete build.
- Request a hop by stable map identifier (filename), not a temporary list index.
- Report accepted, busy, unsupported, failed, and completed results.

**Forge** should own development controls, shortcut handling, optional automatic
update behavior, and its lifecycle registration with Map Loader. Gate in-game
Custom Maps visibility and the hop operation itself on an active Forge capability.
A modules.txt entry alone is insufficient if Forge failed to initialize or has
been deactivated by the loader's context rules.

**Mod Menu** should present the controls and progress. Manual shortcut/config use
should remain possible without Mod Menu. Existing loader configuration storage
can persist settings in `uya/config/forge.<region>.ini`.

Do not let Forge write directly into Map Loader's private globals. If Map Loader
is missing or incompatible, show a useful unavailable status and reject actions.
Use owned registrations so deactivation removes only Forge's capabilities.

## Proposed in-game experience

Open MOD MENU from the pause menu, as with the existing input rules. Do not
re-enable L3 + R3 during gameplay.

While Forge is active:

- The Custom Maps tab becomes available in-game, using the existing centered
  list, descriptions, variable-width tabs, and X-to-select behavior.
- Selecting another custom map requests a hop and closes the menu once accepted.
  Validation failures leave the menu available with an explanation.
- A Forge tab provides **Reload Current Map**, **Refresh Custom Maps**, operation
  status, current filename/version, and the last successfully loaded build ID
  when the exporter supplies one.
- An optional L1 + Up + Circle shortcut requests a reload on the press edge,
  outside Mod Menu, and must consume its gameplay inputs. Holding it must not
  repeatedly trigger reloads.
- Progress captures controller input and releases it on completion or failure.

The Custom Maps list merges retail maps 40-49 alphabetically with custom entries, with
"Retail map" descriptions. Selecting one in staging clears the custom override
and makes it the baseline for subsequent custom selections. During gameplay,
Forge performs a full transition with player reset and HUD setup. `None` remains
staging-only and restores the baseline; choose a named retail entry in-game.
The PINE interface provides the same behavior through command 6.

With Forge inactive, hide the in-game list and reject in-game hop requests;
staging map selection and refresh continue to work normally.

## Additional requested features

- **Optional reload after a successful build:** let Forge trigger the next test
  automatically after the new build is complete and validated. Keep this opt-in;
  queue the request while busy and retain manual reload as an alternative.
- **Remember test position:** optionally restore the player's position and camera
  after reloading the same map. Fall back to a normal spawn when geometry changes
  make the saved position invalid. Do not apply a position from a different map.
- **Reload status/error display:** show pending, validating, loading, completed,
  and failed states. Explain missing files, wrong regions, and incomplete builds
  instead of silently loading old content. Display the actual loaded build ID
  when available so testers can distinguish it from the requested build.

These are requested suggestions for future implementation, not existing features.

## Refresh and automatic discovery

Provide manual Refresh Custom Maps independently of automatic detection. It
should rescan, rebuild the displayed list, preserve the selection by filename,
and report added, removed, or updated maps. Invalid scans retain the last valid
catalog. Keep the loaded map's identity separate from the refreshed list so that
removing its files does not invalidate live state or accidentally select another map.

The scanner supports Local Play staging and supported Forge gameplay contexts.
A successful catalog is reused on later staging visits. Refresh Custom Maps or
a PINE refresh request explicitly checks for added, removed, or updated files.
Deactivation releases the catalog; the next activation scans again.

Suggested automatic detection, in order of preference:

1. An explicit completed-build notification from the editor's external helper.
2. A small exporter-written catalog/build revision file, checked at a modest
   interval while idle. A changed revision triggers a scan.
3. An optional slow periodic scan for existing map packs without such a marker.
   Do not run a full directory scan every frame or during a map transition.

There is no on-disk catalog index. Refresh explicitly enumerates the map files
and rebuilds the retained catalog. An external revision marker would need to be
updated by the exporter to detect changes without another scan.

Version/name/asset-size comparisons detect normal map-pack updates, but can miss
an edit with unchanged version and byte lengths. Recommend a distinct build ID
and content hashes in the export manifest. Compute full hashes on the PC side;
avoid repeatedly hashing large maps on the emulated PS2.

Default behavior should detect changes and report **New build available**.
Automatic reload should be a separate opt-in setting so a build does not interrupt
an ongoing test unexpectedly. Queue/coalesce notifications while busy; do not nest
scans or transitions.

## Proposed exporter/PINE handoff (for the Forge v2 developer)

No editor changes are part of this task. Agree on a small versioned contract with
its developer before implementing the bridge. PINE being enabled does not itself
make UYA capable of a safe map transition or provide filesystem change notifications.

A proposed hostfs convention is `uya/mods/utils/forge/` for bridge/support files.
Continue using Map Loader-compatible map locations until a versioned API explicitly
supports other layouts. Suggested export metadata:

- Protocol version, target game, region, base map, and stable map filename.
- Monotonic request/build ID and map's public version.
- Complete asset list with sizes and hashes.
- A completed-build marker published only after validation and all writes finish.

Never reload from a partially overwritten set of files. A marker alone is not
sufficient if the exporter is still replacing files used by the current build.
Agree on immutable build directories or an explicit prepare/ready handshake so
readers stay on a complete build until the next one is committed. Retain the last
known-good build until transition completion is acknowledged.

PINE could carry requests through a small in-game mailbox owned by Forge:
`refresh`, `reload-current`, or `hop-to-map`, plus request IDs and result/status.
The game consumes requests at a safe update point. Do not use PINE to invoke an
arbitrary function or overwrite the game's transition state from the PC.

The mailbox address must be discoverable through a documented loader/Forge
handshake; module addresses change when the module set is rebuilt. Validate magic,
protocol version, region, capabilities, and session identity after every boot.
Write payload first and a commit sequence last, reject duplicates/stale requests,
and acknowledge completion separately from acceptance. A timeout must not cause
an already accepted hop to run twice. Precise memory layout, PINE client support,
and ordering behavior remain to be designed and verified.

This can support the eventual loop: build -> publish complete assets -> notify ->
validate -> hop/reload -> acknowledge loaded build. Manual menu refresh/reload
should continue to work when no editor or PINE client is connected.

## Transition requirements

- Start with NTSC-U Local Play and an existing known-good custom map.
- Preflight the requested map and complete build before releasing current-level
  resources; block simultaneous loads, scans, and repeated input requests.
- Use UYA's normal teardown/loading path as far as possible. Reset stale map,
  sound, radar, and input state; reinstall overlay hooks only after validating
  the expected retail instructions.
- Preserve the local player roster and intended settings without assuming that
  local split-screen IDs equal online map IDs. Keep valid retail bases 40-49.
- Check every applicable player/controller, not only player one.
- If preflight fails, remain in the current map. If failure occurs after teardown,
  use a verified recovery path; do not promise rollback into a destroyed level.
- Keep in-progress operation resources alive until completion/cancellation is
  safe, even if Forge is disabled or the context changes.
- Maintain the loader's existing online-disable policy. PINE does not override it.
- Verify PAL separately. Do not advertise NTSC-J multiplayer or campaign hopping
  until the corresponding loading backends and transitions exist.

## Suggested milestones and acceptance checks

1. **Manual reload:** reload the active custom map repeatedly from a Forge action
   and the optional shortcut, without visiting staging. Verify sound, radar,
   players, input restoration, and no accumulating memory leaks.
2. **Map hopping:** select another custom map from the in-game list. Exercise
   same-base and different-base maps, supported split-screen modes, and repeated
   hops. Verify selected/loading names and session settings.
3. **Refresh:** add, remove, and update maps while idle; refresh without restarting
   the match. Check selection stability, duplicate requests, incomplete exports,
   missing assets, and the currently loaded map being removed from disk.
4. **Automatic detection:** test completed-build notifications/revisions, rapid
   successive builds, unchanged-size edits, and notifications during loading.
5. **PINE integration:** agree on the contract with Forge v2's developer and test
   reconnects, rebooted sessions, stale IDs, acknowledgement, and error reporting.
6. **Regional qualification:** repeat the relevant runtime checks on PAL before
   declaring it live-qualified. Record any remaining unsupported cases explicitly.

Further optional ideas: show load duration and expose developer diagnostics.
These are secondary to reliable full reloads and hops and are not promises of
implemented functionality.

Retail hopping: automated tests and all regional builds pass. Live NTSC-U checks:
- Temple of Shaar -> retail Korgon Outpost completed with the custom override
  cleared. The user confirmed the retail loading name and HUD.
- Reloading retail Korgon completed with custom assets still disabled.
- Korgon -> Bakisi Mini completed with the custom override re-enabled on base 40.
- The user confirmed gameplay and HUD after the retail reload and return hop.
  Retail entries also appeared correctly; they now share alphabetical ordering
  with custom maps, with `None` first. Other retail bases and split-screen
  hopping have not been live-qualified. PAL coverage is listed above.
