#include "musicplayer.h"
#include "modules/modmenu/include/modmenu.h"

typedef struct musicGroup {
	/* 0x00 */ const char* name;
	/* 0x04 */ u16 wad;
	/* 0x06 */ u8 songs;
	/* 0x07 */ u8 level;
} musicGroup_t;

typedef enum musicPlayback {
	MUSIC_NORMAL,
	MUSIC_LOOP,
	MUSIC_SHUFFLE,
	MUSIC_NEXT
} musicPlayback_e;

static void deactivate(void);
static void nativeMusic(void* context);
static void play(void* context);
static void playback(void* context);
static void pauseTrack(void* context);
static void skipTrack(void* context);
static void updateButtons(void);

const moduleApi_t* musicLoader;
static const modMenuApi_t* menu;
static int tab, selected, playlist, playbackMode;
static int autoPlay;
static int trackView, previousView, repeatBody;
static int playing, paused, requested, lastLevel = -2, lastMode;
static int stopped, buttonState = -1;
static int customPlaying, customScanned;
static int resumePlayback, resumePaused;
static int trackItem, controlsItem;
static int catalogueCount, catalogueGroup, baseCount;
static u32 randomState = 0x1b873593;
static u32 header[512] __attribute__((aligned(64)));
static musicTrack_t catalogue[MUSIC_MAX_TRACKS], base[14];
static char status[64] = "Native level music";
static const moduleConfigField_t settings[] = {
	{"playlist", &playlist, MUSIC_BASE, MUSIC_CUSTOM},
	{"playback", &playbackMode, MUSIC_NORMAL, MUSIC_NEXT},
	{"trackView", &trackView, 0, 1},
	{"autoPlay", &autoPlay, 0, 1}
};
/* Music-WAD IDs/counts from sp-music-to-mp. Only music, not dialogue.
 * Campaign descriptors alternate intro and looping-body stereo pairs. */
static const musicGroup_t groups[] = {
	{"Multiplayer", 0x54d, 14, 39},
	{"Veldin", 0x3f9, 2, 1},
	{"Florana", 0x403, 4, 2},
	{"Starship Phoenix", 0x40d, 3, 3},
	{"Marcadia", 0x417, 3, 4},
	{"Daxx", 0x421, 4, 5},
	{"Phoenix Under Attack", 0x42b, 1, 6},
	{"Annihilation Nation", 0x435, 4, 7},
	{"Aquatos", 0x43f, 1, 8},
	{"Tyhrranosis", 0x449, 1, 9},
	{"Zeldrin Starport", 0x453, 1, 10},
	{"Obani Gemini", 0x45d, 1, 11},
	{"Blackwater City", 0x467, 1, 12},
	{"Holostar Studios", 0x471, 2, 13},
	{"Koros", 0x47b, 1, 14},
	{"Metropolis", 0x485, 1, 16},
	{"Crash Site", 0x48f, 1, 17},
	{"Aridia", 0x499, 1, 18},
	{"Qwark's Hideout", 0x4a3, 1, 19},
	{"Biobliterator", 0x4ad, 1, 20},
	{"Obani Draco", 0x4b7, 2, 21},
	{"Mylon", 0x4c1, 1, 22},
	{"Holostar Clank", 0x4cb, 1, 23},
	{"Insomniac Museum", 0x4d5, 1, 24},
	{"Metropolis Rangers", 0x4df, 1, 26},
	{"Aquatos Base", 0x4e9, 1, 27},
	{"Aquatos Sewers", 0x4f3, 1, 28},
	{"Ranger Mission", 0x4fd, 1, 29},
	{"Vidcomic 1", 0x507, 1, 30},
	{"Vidcomic 2", 0x511, 1, 31},
	{"Vidcomic 5", 0x51b, 1, 32},
	{"Vidcomic 3", 0x525, 1, 33},
	{"Vidcomic 4", 0x52f, 1, 34},
	{"Vidcomic 6", 0x539, 1, 35},
	{"Vidcomic 7", 0x543, 1, 36}
};

/*
 * NAME : trackCount
 * DESCRIPTION : Count the tracks in the selected playlist.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int trackCount(void)
{
	int i, count = 0;
	int raw = playlist == MUSIC_BASE ? baseCount : catalogueCount;
	musicTrack_t* tracks = playlist == MUSIC_BASE ? base : catalogue;
	if (playlist == MUSIC_CUSTOM)
		return musicCustomCount();
	for (i = 0; i < raw; ++i)
		if (trackView || tracks[i].part != MUSIC_BODY)
			++count;
	return count;
}

/*
 * NAME : trackAt
 * DESCRIPTION : Resolve a visible song to its disc descriptors.
 * NOTES : Simple view hides loop bodies behind their intro entry.
 * ARGS : Visible index and view.
 * RETURN : Disc track, or NULL.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static musicTrack_t* trackAt(int index, int view)
{
	int i, count = playlist == MUSIC_BASE ? baseCount : catalogueCount;
	musicTrack_t* tracks = playlist == MUSIC_BASE ? base : catalogue;
	for (i = 0; i < count; ++i) {
		if (!view && tracks[i].part == MUSIC_BODY)
			continue;
		if (index-- == 0)
			return &tracks[i];
	}
	return 0;
}

/*
 * NAME : viewLabel
 * DESCRIPTION : Name the track-list presentation choices.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* viewLabel(void* context, int value)
{
	(void)context;
	return value ? "Complex" : "Simple";
}

/*
 * NAME : changeView
 * DESCRIPTION : Preserve the selected song when changing track view.
 * NOTES : Changing presentation does not restart playback.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void changeView(void* context)
{
	int i;
	(void)context;
	if (playlist != MUSIC_CUSTOM) {
		musicTrack_t* track = trackAt(selected, previousView);
		if (track && !trackView && track->part == MUSIC_BODY)
			--track;
		for (i = 0; i < trackCount(); ++i)
			if (trackAt(i, trackView) == track) {
				selected = i;
				break;
			}
	}
	previousView = trackView;
}

/*
 * NAME : playlistLabel
 * DESCRIPTION : Name the playlist selector choices.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* playlistLabel(void* context, int index)
{
	static const char* names[] = {"Default / Base", "All", "Custom"};
	(void)context;
	return names[index];
}

/*
 * NAME : playbackLabel
 * DESCRIPTION : Name the track completion behaviors.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* playbackLabel(void* context, int index)
{
	static const char* names[] = {"Normal", "Loop Track", "Shuffle", "Next Track"};
	(void)context;
	return names[index];
}

/*
 * NAME : changePlayback
 * DESCRIPTION : Restore normal music or take control of the selected track.
 * NOTES : Normal leaves the game's scripted track choices intact.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void changePlayback(void* context)
{
	if (playbackMode == MUSIC_NORMAL)
		nativeMusic(context);
	else if (!stopped && !paused)
		play(context);
}

/*
 * NAME : trackLabel
 * DESCRIPTION : Show the selected song name.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* trackLabel(void* context, int index)
{
	static char label[64];
	musicTrack_t* track;
	(void)context;
	if (index < 0 || index >= trackCount())
		return "No tracks available";
	if (playlist == MUSIC_CUSTOM)
		return musicCustomName(index);
	track = trackAt(index, trackView);
	musicText(label, track->name, 0, !trackView || track->part == MUSIC_STANDALONE ? "" :
	    track->part == MUSIC_INTRO ? " (intro)" : " (loop)");
	return label;
}

/*
 * NAME : statusLabel
 * DESCRIPTION : Show playback or discovery status.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* statusLabel(void* context, int index)
{
	(void)context;
	(void)index;
	return status;
}

/*
 * NAME : chooseTrack
 * DESCRIPTION : Choose the next sequential or nonrepeating random song.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void chooseTrack(int direction)
{
	int count = trackCount();
	if (!count)
		return;
	if (!direction && count > 1) {
		randomState ^= randomState << 13;
		randomState ^= randomState >> 17;
		randomState ^= randomState << 5;
		selected = (selected + 1 + randomState % (count - 1)) % count;
	} else {
		selected = (selected + direction + count) % count;
	}
	requested = 1;
	paused = 0;
	stopped = 0;
	repeatBody = 0;
}

/*
 * NAME : finishTrack
 * DESCRIPTION : Apply the selected behavior after a manual track ends.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void finishTrack(void)
{
	if (playbackMode == MUSIC_NORMAL)
		nativeMusic(0);
	else if (playbackMode == MUSIC_LOOP) {
		repeatBody = 1;
		requested = 1;
	} else
		chooseTrack(playbackMode == MUSIC_SHUFFLE ? 0 : 1);
}

/*
 * NAME : changeSelection
 * DESCRIPTION : Apply a playlist or song selection.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void changeSelection(void* changedPlaylist)
{
	repeatBody = 0;
	if (changedPlaylist) {
		selected = 0;
		if (customPlaying) {
			musicCustomClose();
			customPlaying = 0;
		}
	}
	if (selected >= trackCount())
		selected = selected == MUSIC_MAX_TRACKS - 1 && trackCount() ? trackCount() - 1 : 0;
	if (!changedPlaylist)
		play(0);
	else if (playing)
		requested = 1;
}

/*
 * NAME : play
 * DESCRIPTION : Start the selection or resume paused playback.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void play(void* context)
{
	(void)context;
	if (!trackCount() || !musicBackendAcquire())
		return;
	stopped = 0;
	if (paused) {
		paused = 0;
		if (customPlaying)
			musicCustomPause(0);
		else
			musicBackendPause(0);
	} else {
		requested = 1;
	}
	playing = 1;
}

/*
 * NAME : playback
 * DESCRIPTION : Stop manual playback or play the selected song.
 * NOTES : Stopping keeps scripted music silent until Play or Return to Level Music.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void playback(void* context)
{
	int nativePlaying = !playing && !stopped && musicBackendStream()->handle;
	if ((!playing && !nativePlaying) || paused) {
		play(context);
		return;
	}
	if (!musicBackendAcquire())
		return;
	musicCustomClose();
	customPlaying = 0;
	playing = paused = requested = 0;
	stopped = 1;
	musicText(status, "Stopped", 0, "");
}

/*
 * NAME : updateButtons
 * DESCRIPTION : Keep the compact playback row in sync with playback state.
 * NOTES : Left/right focuses a button; X activates it.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void updateButtons(void)
{
	int active = !paused && (playing || (!stopped && musicBackendStream()->handle));
	modMenuButton_t buttons[] = {
		{"<<", skipTrack, (void*)-1},
		{active ? "Stop" : "Play", playback, 0},
		{"Pause", pauseTrack, 0},
		{">>", skipTrack, (void*)1}
	};
	if (tab && buttonState != active) {
		menu->setItemButtons(controlsItem, buttons, sizeof(buttons) / sizeof(buttons[0]));
		buttonState = active;
	}
}

/*
 * NAME : pauseTrack
 * DESCRIPTION : Pause music without pausing other game audio.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void pauseTrack(void* context)
{
	(void)context;
	if (!playing && !stopped && musicBackendStream()->handle && musicBackendAcquire())
		playing = 1;
	if (playing) {
		paused = 1;
		if (customPlaying)
			musicCustomPause(1);
		else
			musicBackendPause(1);
	}
}

/*
 * NAME : skipTrack
 * DESCRIPTION : Select and play the previous or next song.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void skipTrack(void* context)
{
	chooseTrack((int)context);
	play(0);
}

/*
 * NAME : nativeMusic
 * DESCRIPTION : Return music selection to the game.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void nativeMusic(void* context)
{
	(void)context;
	musicCustomClose();
	customPlaying = 0;
	musicBackendRelease();
	playing = paused = requested = stopped = 0;
	resumePlayback = resumePaused = 0;
	repeatBody = 0;
	musicText(status, "Native level music", 0, "");
}

/*
 * NAME : refreshCustom
 * DESCRIPTION : Request a fresh custom-song directory scan.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void refreshCustom(void* context)
{
	(void)context;
	musicCustomRefresh();
	customScanned = 0;
}

/*
 * NAME : addDiscGroup
 * DESCRIPTION : Read one disc soundtrack into the All playlist.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void addDiscGroup(int index)
{
	const musicGroup_t* group = &groups[index];
	int song, variant, n;
	if (!musicBackendReadWad(group->wad, header))
		return;
	for (song = 0; song < group->songs; ++song) {
		int campaign = group->level < 39;
		for (variant = 0; variant <= campaign; ++variant) {
			u32* source = header + 2 + (campaign ? song * 8 + variant * 4 : song * 4);
			musicTrack_t* track;
			if (catalogueCount == MUSIC_MAX_TRACKS || !source[0] || !source[2])
				return;
			track = &catalogue[catalogueCount++];
			track->part = campaign ? (variant ? MUSIC_BODY : MUSIC_INTRO) : MUSIC_STANDALONE;
			for (n = 0; n < 4; ++n)
				track->channels[n] = source[n] + ((n & 1) ? 0 : header[1]);
			musicText(track->name, group->name, song + 1, "");
		}
	}
}

/*
 * NAME : captureBase
 * DESCRIPTION : Snapshot this level's original stereo descriptors.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void captureBase(void)
{
	const u32* table = musicBackendTable();
	static const char* multiplayerNames[] = {"Online Lobby", "Game Staging", "Victory", "Loss"};
	const char* name = "Level track";
	int n, song, count = 1;
	if (musicLoader->mode == UYA_MODE_MULTIPLAYER) {
		count = 14;
	} else {
		int level = musicBackendLevel();
		for (n = 0; n < (int)(sizeof(groups) / sizeof(groups[0])); ++n) {
			if (groups[n].level == level) {
				count = groups[n].songs;
				name = groups[n].name;
				break;
			}
		}
		if (level >= 0)
			count *= 2;
	}
	baseCount = 0;
	for (song = 0; song < count; ++song) {
		const u32* source = table + song * 4;
		musicTrack_t* track = &base[baseCount];
		if (!source[0] || !source[2])
			continue;
		for (n = 0; n < 4; ++n)
			track->channels[n] = source[n] + ((n & 1) ? 0 : musicBackendSector());
		track->part = musicLoader->mode == UYA_MODE_MULTIPLAYER || musicBackendLevel() < 0 ?
		    MUSIC_STANDALONE : (song & 1) ? MUSIC_BODY : MUSIC_INTRO;
		if (musicLoader->mode == UYA_MODE_MULTIPLAYER) {
			musicText(track->name, song < 4 ? multiplayerNames[song] : "Multiplayer track", song < 4 ? 0 : song - 3, "");
		} else {
			musicText(track->name, name, musicBackendLevel() < 0 ? 1 : song / 2 + 1, "");
		}
		++baseCount;
	}
}

/*
 * NAME : followNativeTrack
 * DESCRIPTION : Match the current native song to the selected disc playlist.
 * NOTES : Native track IDs address eight-byte channel entries, two per stereo song.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void followNativeTrack(void)
{
	int track = musicBackendStream()->track;
	int i, n;
	const u32* source;
	if (playing || stopped || requested || playlist == MUSIC_CUSTOM || track < 0 || track >= 28)
		return;
	source = musicBackendTable() + track * 2;
	for (i = 0; i < trackCount(); ++i) {
		musicTrack_t* track = trackAt(i, trackView);
		int part, parts = !trackView && track->part == MUSIC_INTRO ? 2 : 1;
		for (part = 0; part < parts; ++part) {
			for (n = 0; n < 4; ++n) {
				u32 value = source[n] + ((n & 1) ? 0 : musicBackendSector());
				if (track[part].channels[n] != value)
					break;
			}
			if (n == 4) {
				selected = i;
				return;
			}
		}
	}
}

/*
 * NAME : registerMenu
 * DESCRIPTION : Register standard Music Player menu controls.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void registerMenu(void)
{
	static int unused;
	modMenuItem_t items[] = {
		{"Playlist", MODMENU_RANGE, &playlist, MUSIC_BASE, MUSIC_CUSTOM, 1, changeSelection, (void*)1},
		{"Track", MODMENU_RANGE, &selected, 0, MUSIC_MAX_TRACKS - 1, 1, changeSelection, 0},
		{"", MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0},
		{"Playback Mode", MODMENU_RANGE, &playbackMode, MUSIC_NORMAL, MUSIC_NEXT, 1, changePlayback, 0},
		{"Track View", MODMENU_RANGE, &trackView, 0, 1, 1, changeView, 0},
		{"Return to Level Music", MODMENU_ACTION, 0, 0, 0, 0, nativeMusic, 0},
		{"Refresh Custom Songs", MODMENU_ACTION, 0, 0, 0, 0, refreshCustom, 0},
		{"Status", MODMENU_RANGE, &unused, 0, 0, 1, 0, 0}
	};
	int handles[8], i;
	menu = modmenuFind(musicLoader);
	if (!menu)
		return;
	tab = menu->addTab("musicplayer", "Music Player", UYA_MODE_BOTH);
	if (tab < 0) {
		tab = 0;
		return;
	}
	for (i = 0; i < 8; ++i) {
		handles[i] = menu->addItem(tab, &items[i]);
		if (handles[i] < 0) {
			menu->removeTab(tab);
			tab = 0;
			return;
		}
	}
	trackItem = handles[1];
	controlsItem = handles[2];
	buttonState = -1;
	updateButtons();
	menu->setItemValueLabel(handles[0], playlistLabel);
	menu->setItemValueLabel(trackItem, trackLabel);
	menu->setItemValueLabel(handles[3], playbackLabel);
	menu->setItemValueLabel(handles[4], viewLabel);
	menu->setItemValueLabel(handles[7], statusLabel);
	menu->setItemEnabled(handles[7], 0);
	menu->setTabAction(tab, "Refresh Custom Songs", refreshCustom, 0);
	menu->setTabContexts(tab, UYA_CONTEXT_ALL);
}

/*
 * NAME : deactivate
 * DESCRIPTION : Release music hooks and remove the menu tab.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void deactivate(void)
{
	nativeMusic(0);
	if (menu && tab)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	lastLevel = -2;
}

/*
 * NAME : moduleInit
 * DESCRIPTION : Initialize music playback and optional settings.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int moduleInit(const moduleApi_t* api)
{
	musicLoader = api;
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	api->loadConfig("musicplayer", settings, sizeof(settings) / sizeof(settings[0]));
	previousView = trackView;
	api->setLifecycle(0, deactivate);
	return 0;
}

/*
 * NAME : moduleUpdate
 * DESCRIPTION : Follow level changes, playback requests, and completed songs.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleUpdate(void)
{
	int ready = musicBackendReady();
	if (!ready) {
		/* Disc/custom playlists survive loading, but their old streams cannot. */
		if ((playing || requested) && playlist != MUSIC_BASE) {
			resumePlayback = 1;
			resumePaused = paused;
		}
		musicCustomClose();
		customPlaying = 0;
		musicBackendRelease();
		playing = paused = requested = stopped = 0;
		lastLevel = -2;
		return;
	}
	if (lastLevel != musicBackendLevel() || lastMode != (int)musicLoader->mode) {
		int resume = resumePlayback;
		int wasPaused = resumePaused;
		int track = selected;
		nativeMusic(0);
		lastLevel = musicBackendLevel();
		lastMode = musicLoader->mode;
		selected = resume ? track : 0;
		captureBase();
		requested = resume || autoPlay;
		resumePlayback = resume;
		resumePaused = resume && wasPaused;
	}
	if (!tab)
		registerMenu();
	musicBackendControl();
	if (stopped)
		musicBackendSilence();
	if (playlist == MUSIC_CUSTOM && !customScanned) {
		int result = musicCustomScan();
		if (result <= 0)
			musicText(status, result < 0 ? "Cannot scan uya/music" : "Reading custom songs", 0, "");
		if (result)
			customScanned = 1;
		if (result > 0)
			musicText(status, "Custom songs ready", 0, "");
	}
	if (playlist == MUSIC_ALL && catalogueGroup < (int)(sizeof(groups) / sizeof(groups[0]))) {
		musicText(status, "Reading disc music", catalogueGroup + 1, "");
		addDiscGroup(catalogueGroup++);
		if (catalogueGroup == (int)(sizeof(groups) / sizeof(groups[0])))
			musicText(status, "Disc songs ready", 0, "");
		return;
	}
	if (selected >= trackCount() && trackCount())
		selected = 0;
	followNativeTrack();
	/* Let retail establish the new sound stream before taking it over. The
	 * overlay and scene-ready flag can become valid before audio setup ends. */
	if (resumePlayback) {
		musicStream_t* stream = musicBackendStream();
		if (!stream->handle || stream->handle == 0xffffffff ||
		    (stream->status != 4 && stream->status != 8))
			return;
		resumePlayback = 0;
	}
	if (requested && trackCount() && (playlist != MUSIC_CUSTOM || customScanned) && musicBackendAcquire()) {
		int result;
		musicCustomClose();
		customPlaying = 0;
		if (playlist == MUSIC_CUSTOM)
			result = musicBackendSilence() ? musicCustomOpen(selected) : 0;
		else {
			musicTrack_t* track = trackAt(selected, trackView);
			const musicTrack_t* body = !trackView && track->part == MUSIC_INTRO ? track + 1 : 0;
			if (repeatBody && body) {
				track = (musicTrack_t*)body;
				body = 0;
			}
			result = musicBackendSilence() ? musicDiscOpen(track, body, playbackMode == MUSIC_LOOP) : 0;
		}
		if (result > 0) {
			requested = 0;
			playing = 1;
			customPlaying = 1;
			if (resumePaused) {
				paused = 1;
				musicCustomPause(1);
				resumePaused = 0;
			}
		} else if (result < 0) {
			nativeMusic(0);
			musicText(status, "Unsupported or unreadable song", 0, "");
		}
	} else if (playing && customPlaying) {
		int result = musicCustomTick();
		if (result > 0)
			finishTrack();
		else if (result < 0) {
			nativeMusic(0);
			musicText(status, "Custom audio stream failed", 0, "");
		}
	} else if (playing && !paused && !musicBackendStream()->handle && !musicBackendStream()->status) {
		finishTrack();
	}
	if (playing) {
		if (!customPlaying)
			musicBackendPause(paused);
		musicText(status, paused ? "Paused" : "Playing", 0, "");
	}
	if (tab) {
		int available = trackCount() > 0;
		menu->setItemEnabled(trackItem, available);
		menu->setItemEnabled(controlsItem, available || playing || stopped);
		updateButtons();
	}
}
