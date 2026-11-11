# Music Player

Enable `musicplayer campaign,local` in `uya/config/modules.txt`, with `modmenu`
enabled for its controls. Builds for NTSC-U and PAL. Normal level music stays
under the game's control by default.

## Controls

- **Playlist:** Default / Base, All, or Custom.
- **Track:** follows the currently playing native song, including staging and
  game-end music. Left/Right selects and plays a different song.
- **Track View:** Simple (default) lists each campaign song once and prebuffers
  its loop behind its intro for a continuous transition. Complex lists intro
  and loop separately. Custom files and multiplayer songs are unchanged.
  Loop Track uses the game's native looping-body flag after the intro.
- One centered row of `<<`, `Play/Stop`, `Pause`, and `>>` buttons.
  Left/Right focuses a button; X activates it. Play resumes paused music;
  Stop silences music until you play again or return to level music.
  Dialogue and sound effects continue normally.
- **Playback Mode:** Normal leaves scripted music unchanged; a manually
  selected song returns to normal music when it ends. Loop Track repeats the
  selection, Shuffle chooses a different random song, and Next Track advances
  through the playlist in order. Normal is the default.
- **Return to Level Music:** restore the game's music selection.
- **Refresh Custom Songs:** rescan the folder; Square does the same while the
  Music Player tab is focused.

**Default / Base** contains the current level's music. The multiplayer list
includes all 14 stereo tracks: four menu/result tracks and ten game tracks.
Complex view distinguishes each campaign song's intro and looping body.

**All** reads music descriptors from the installed disc. Dialogue entries are
excluded. Simple view combines campaign intro/body pairs; Complex exposes
both parts. Labels identify their source rather than official soundtrack titles.

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
64 KiB EE ring buffers. Songs are not loaded entirely into memory. The
shared `uya/mods/utils/mapscan.irx` must be from this build; it provides
independent map and music directory services and is loaded when needed.

See [decoders/README.md](decoders/README.md) for adding audio formats.

## Without Mod Menu

The loader can optionally read `uya/config/musicplayer.<region>.ini`:

```ini
playlist=2
playback=2
trackView=0
autoPlay=1
```

Playlist values are `0` Base, `1` All, and `2` Custom. Playback values are `0`
Normal, `1` Loop Track, `2` Shuffle, and `3` Next Track. `autoPlay` defaults to
off. Track View values are `0` Simple and `1` Complex. When enabled, autoplay
starts on entering a supported level even without
Mod Menu. Menu selections otherwise last for the current session; they are
not automatically written to disk.

Campaign entry points are selected per level and region; the soundtrack lookup
uses explicit level IDs, including the gaps in campaign numbering. These
addresses were checked against the extracted level code, which does not require
a full RAM dump of every map. Live campaign testing is still required.

Online activation is opt-in through `modules.txt`; it has not been live-tested
with Horizon. NTSC-J is not enabled by this module.
