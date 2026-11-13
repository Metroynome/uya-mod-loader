# Music Player

Enable `musicplayer campaign,local` in `uya/config/modules.txt`, with `modmenu`
enabled for its controls. Builds for NTSC-U and PAL, plus NTSC-J campaign. Normal level music stays
under the game's control by default.

Music Player requires Mod Menu and usable expanded RAM. The loader leaves it
inactive and displays a BIOS-warning-style popup listing only missing
requirements. If RAM is unavailable, its audio buffers are not allocated.
Enable Mod Menu in the same contexts as Music Player, then restart.

## Controls

- **Playlist:** Default, Game, Custom, Game (Shuffle), or Custom (Shuffle).
  Game and Custom advance to the next song in order. Their Shuffle variants
  choose a different random song when the current song ends.
- **Track:** follows the currently playing native song, including staging and
  game-end music. Left/Right selects and plays a different song.
- **Track View:** Simple (default) lists each campaign song once and prebuffers
  its loop behind its intro for a continuous transition. Complex lists intro
  and loop separately. Custom files and multiplayer songs are unchanged.
- One centered row of `<<`, `Play/Stop`, `Pause`, and `>>` buttons.
  Left/Right focuses a button; X activates it. Play resumes paused music;
  Stop silences music until you play again or choose Default.
  Dialogue and sound effects continue normally.
- **Default Playlists:** choose any playlist separately for Campaign, Online
  Lobby, Staging, and In Game. Defaults apply on entry, leaving manual changes
  alone while you stay on that screen. Default uses the game's normal soundtrack;
  Plain Game/Custom start at the first song. Shuffle chooses a random first song
  and avoids repeating the previous start when more than one song is available.
  A same-map reload resumes a plain playlist; Shuffle chooses a new starting song. Campaign defaults apply when loading a save;
  manual playback continues across planet transitions.
  Saved defaults also offer **Game + Custom** (game songs, then custom files)
  and **Game + Custom (Shuffle)** (shuffle across both sources). These two
  choices are not added to the main Playlist selector.
- **Load Settings / Save Settings:** centered side-by-side buttons. Settings
  load automatically at startup; Save writes them to the regional config file.
  Loading settings also reapplies the current screen's default.
- **Refresh Music:** rescan the folder; Square does the same while the
  Music Player tab is focused.

**Default** returns playback to the game, including campaign intro-to-loop
behavior. Its Track list contains the current level's music. The multiplayer list
includes all 14 stereo tracks: four menu/result tracks and ten game tracks.
Complex view distinguishes each campaign song's intro and looping body.

**Game** reads music descriptors from the installed disc. Dialogue entries are
excluded. Simple view combines campaign intro/body pairs; Complex exposes
both parts. Labels identify their source rather than official soundtrack titles.
The Museum's copy of Vidcomic 1 is listed once, under Vidcomic 1. Holostar's
Backlot and Grav-Boot recordings remain separate tracks.

Selected in-game playlists start at the native panoramic music cue, before
player control. The initial vanilla music stream is suppressed for that cue;
speech and effects are unchanged. Music files/catalogues needed by the saved
in-game default are discovered in the multiplayer menus before loading.

The saved `defaultCampaign` choice controls music when loading a campaign save.
Older settings initially use the saved Playlist choice for this default.

## Custom songs

Put files directly in `hostfs/uya/music/` (or `uya/music/` under your configured
host root). Subfolders are not scanned. No playlist/index file or conversion
script is required.

- `.mp3`: MPEG Layer III, mono/stereo; ID3 metadata up to 1 MiB.
- `.wav`: uncompressed integer PCM, 8/16/24/32-bit, mono/stereo.
- `.vag`: game-compatible mono `VAGp` PS2 ADPCM files with a 48-byte header.

Sample rates from 8–48 kHz are accepted. Float/compressed WAV, interleaved
stereo VAG variants, and complete music WAD archives are not supported.
The catalogue holds up to 160 files; filenames must fit 95 bytes.

Custom audio is decoded in C and fed to the existing sound driver through two
module-owned EE ring buffers. Multiplayer uses 64 KiB per channel; campaign
uses 128 KiB to accommodate its larger startup read. Refilling continues while
sound callbacks are pending, with small decode batches spread across updates.
Compressed input is read in batches instead of one file read per MP3 frame.
Songs are not loaded entirely into memory. The
shared `uya/mods/utils/mapscan.irx` must be from this build; it provides
independent map and music directory services and is loaded when needed.

Use expanded RAM with the bundled module set: the campaign-capable Music Player
currently reserves about 354 KiB at runtime, including both rings. The full
menu/module setup does not fit the normal low-memory pool with these buffers.

See [decoders/README.md](decoders/README.md) for adding audio formats.

## Without Mod Menu

The loader can optionally read `uya/config/musicplayer.<region>.ini`:

```ini
playlist=2
trackView=0
autoPlay=1
defaultLobby=0
defaultStaging=2
defaultGame=1
```

Playlist values are `0` Base, `1` All, `2` Custom, `3` Game (Shuffle), and
`4` Custom (Shuffle). `autoPlay` defaults to off. Track View values are `0` Simple and `1` Complex. When enabled, autoplay
starts on entering a supported campaign level even without Mod Menu.
Multiplayer uses `defaultLobby`, `defaultStaging`, and `defaultGame`, each with
the same five playlist values. All three default to Base.
Save Settings writes the selected options; changes are not saved implicitly.
The old `playback` key is no longer used. Loop Track is hidden, with its
continuation code retained as comments for future use.

Campaign entry points are selected per level and region; the soundtrack lookup
uses explicit level IDs, including the gaps in campaign numbering. These
addresses were checked against the extracted level code, which does not require
a full RAM dump of every map. NTSC-U campaign playback has been tested; PAL and NTSC-J live checks are pending.

## Horizon online play

Add `online` to Music Player's entry in `modules.txt` to keep it active online.
Use PCSX2's 128 MiB RAM option with a compatible BIOS (the tested setup uses
BIOS 2.30). Horizon reserves `0x000c8000–0x000fffff`; Music Player's audio and
decoder buffers must stay outside that range. The normal 32 MiB module layout
is not qualified for Horizon with Music Player installed.

While Music Player owns playback, its independent disc descriptors and native
Play/Queue hooks take priority over Horizon's Single Player Music option.
Selecting Default restores native track selection to the game and Horizon. No Horizon configuration flag is changed.

NTSC-U online testing with Horizon confirmed custom playback starts during the
panorama and continues in-game with Single Player Music enabled. The tested
modules used expanded RAM. PAL online testing remains pending; NTSC-J multiplayer is not
enabled by this module.

The campaign backend uses librac3 music helpers, and multiplayer uses libuya
compact music tables. Run `docker-init.sh` in a fresh container, or rebuild the
Docker image, to fetch the pinned library revisions used by this module.
