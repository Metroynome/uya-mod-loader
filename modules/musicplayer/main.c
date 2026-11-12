#include "musicplayer.h"
#include "modules/modmenu/include/modmenu.h"

typedef struct musicGroup {
	/* 0x00 */ const char* name;
	/* 0x04 */ u16 wad;
	/* 0x06 */ u8 songs;
	/* 0x07 */ u8 level;
} musicGroup_t;

static void deactivate(void);
static void nativeMusic(void* context);
static void play(void* context);
static void playback(void* context);
static void pauseTrack(void* context);
static void skipTrack(void* context);
static void updateButtons(void);

const moduleApi_t* musicLoader;
static const modMenuApi_t* menu;
static int tab, selected, playlist;
static int autoPlay;
static int defaultCampaign, defaultLobby, defaultStaging, defaultGame, lastContext = -1;
static int trackView, previousView;
static int playing, paused, requested, lastLevel = -2, lastMode;
static int stopped, buttonState = -1;
static int customPlaying, customScanned;
static int resumePlayback, resumePaused, shuffleStart;
static int lastShuffleStart[3] = {-1, -1, -1};
static int trackItem, controlsItem;
static int catalogueCount, catalogueGroup, baseCount;
static u32 randomState = 0x1b873593;
static u32 header[512] __attribute__((aligned(64)));
static musicTrack_t catalogue[MUSIC_MAX_TRACKS], base[14];
static char status[64] = "Native level music";
static const moduleConfigField_t settings[] = {
	{"playlist", &playlist, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE},
	{"trackView", &trackView, 0, 1},
	{"autoPlay", &autoPlay, 0, 1},
	{"defaultCampaign", &defaultCampaign, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE},
	{"defaultLobby", &defaultLobby, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE},
	{"defaultStaging", &defaultStaging, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE},
	{"defaultGame", &defaultGame, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE}
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
 * NAME : customPlaylist
 * DESCRIPTION : Identify either custom-file playlist choice.
 * NOTES :
 * ARGS :
 * RETURN : Nonzero for Custom or Custom (Shuffle).
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int customPlaylist(void)
{
	return playlist == MUSIC_CUSTOM || playlist == MUSIC_CUSTOM_SHUFFLE;
}

/*
 * NAME : includesCustom
 * DESCRIPTION : Identify playlist choices that include custom files.
 * NOTES :
 * ARGS :
 * RETURN : Nonzero when custom songs are included.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int includesCustom(int choice)
{
	return choice == MUSIC_CUSTOM || choice == MUSIC_CUSTOM_SHUFFLE ||
	    choice == MUSIC_COMBINED || choice == MUSIC_COMBINED_SHUFFLE;
}

/*
 * NAME : includesGame
 * DESCRIPTION : Identify playlist choices that include the game catalogue.
 * NOTES :
 * ARGS :
 * RETURN : Nonzero when disc songs are included.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int includesGame(int choice)
{
	return choice == MUSIC_GAME || choice == MUSIC_GAME_SHUFFLE ||
	    choice == MUSIC_COMBINED || choice == MUSIC_COMBINED_SHUFFLE;
}

/*
 * NAME : shufflePlaylist
 * DESCRIPTION : Identify playlists that choose the next song randomly.
 * NOTES :
 * ARGS :
 * RETURN : Nonzero for a Shuffle choice.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int shufflePlaylist(void)
{
	return playlist == MUSIC_GAME_SHUFFLE || playlist == MUSIC_CUSTOM_SHUFFLE ||
	    playlist == MUSIC_COMBINED_SHUFFLE;
}

/*
 * NAME : discTrackCount
 * DESCRIPTION : Count visible game tracks in the selected playlist.
 * NOTES :
 * ARGS :
 * RETURN : Disc song count for the requested view.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int discTrackCount(int view)
{
	int i, count = 0;
	int raw = playlist == MUSIC_BASE ? baseCount : catalogueCount;
	musicTrack_t* tracks = playlist == MUSIC_BASE ? base : catalogue;
	if (customPlaylist())
		return 0;
	for (i = 0; i < raw; ++i)
		if (view || tracks[i].part != MUSIC_BODY)
			++count;
	return count;
}

/*
 * NAME : customTrackIndex
 * DESCRIPTION : Resolve a combined playlist selection to a custom file.
 * NOTES :
 * ARGS :
 * RETURN : Custom file index, or -1 for a disc track.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int customTrackIndex(int index)
{
	if (!includesCustom(playlist))
		return -1;
	index -= discTrackCount(trackView);
	return index >= 0 && index < musicCustomCount() ? index : -1;
}

/*
 * NAME : trackCount
 * DESCRIPTION : Count all visible songs in the selected playlist.
 * NOTES :
 * ARGS :
 * RETURN : Song count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int trackCount(void)
{
	return discTrackCount(trackView) + (includesCustom(playlist) ? musicCustomCount() : 0);
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
	int custom = includesCustom(playlist) ? selected - discTrackCount(previousView) : -1;
	(void)context;
	if (custom >= 0) {
		selected = discTrackCount(trackView) + custom;
	} else if (!customPlaylist()) {
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
	static const char* names[] = {"Default", "Game", "Custom", "Game (Shuffle)", "Custom (Shuffle)",
	    "Game + Custom", "Game + Custom (Shuffle)"};
	(void)context;
	return names[index];
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
	if (customTrackIndex(index) >= 0)
		return musicCustomName(customTrackIndex(index));
	track = trackAt(index, trackView);
	musicText(label, track->name, 0, !trackView || track->part == MUSIC_STANDALONE ? "" :
	    track->part == MUSIC_INTRO ? " (intro)" : " (loop)");
	return label;
}

/*
 * NAME : randomValue
 * DESCRIPTION : Advance the playlist's random state.
 * NOTES :
 * ARGS :
 * RETURN : Pseudorandom value.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 randomValue(void)
{
	randomState ^= randomState << 13;
	randomState ^= randomState >> 17;
	randomState ^= randomState << 5;
	return randomState;
}

/*
 * NAME : chooseShuffleStart
 * DESCRIPTION : Randomize the first song when entering a shuffled playlist.
 * NOTES : Mix the EE cycle counter and avoid the previous start when possible.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void chooseShuffleStart(void)
{
	u32 seed;
	int count = trackCount(), source = playlist >= MUSIC_COMBINED ? 2 : customPlaylist() != 0;
	if (!shuffleStart || !count)
		return;
	__asm__ volatile("mfc0 %0, $9" : "=r"(seed));
	randomState ^= seed;
	if (!randomState)
		randomState = 0x1b873593;
	selected = randomValue() % count;
	if (count > 1 && selected == lastShuffleStart[source])
		selected = (selected + 1) % count;
	lastShuffleStart[source] = selected;
	shuffleStart = 0;
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
		selected = (selected + 1 + randomValue() % (count - 1)) % count;
	} else {
		selected = (selected + direction + count) % count;
	}
	requested = 1;
	paused = 0;
	stopped = 0;
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
	/* Loop Track retained for later:
	 * else if (playbackMode == MUSIC_LOOP) {
	 *     repeatBody = 1;
	 *     requested = 1;
	 * }
	 */
	if (playlist == MUSIC_BASE)
		nativeMusic(0);
	else
		chooseTrack(shufflePlaylist() ? 0 : 1);
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
	if (changedPlaylist && playlist == MUSIC_BASE) {
		nativeMusic((void*)1);
		return;
	}
	if (changedPlaylist) {
		selected = 0;
		if (customPlaying) {
			musicCustomClose();
			customPlaying = 0;
		}
	}
	if (selected >= trackCount())
		selected = selected == MUSIC_MAX_TRACKS * 2 - 1 && trackCount() ? trackCount() - 1 : 0;
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
 * NOTES : Stopping keeps scripted music silent until Play or Default / Base.
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
	musicCustomClose();
	customPlaying = 0;
	musicBackendRelease(context != 0);
	playing = paused = requested = stopped = 0;
	resumePlayback = resumePaused = shuffleStart = 0;
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
	/* Museum reuses Vidcomic 1's intro and body; list that song once in All. */
	if (group->level == 24)
		return;
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
	if (playing || stopped || requested || customPlaylist() || track < 0 || track >= 28)
		return;
	source = musicBackendTable() + track * 2;
	for (i = 0; i < trackCount(); ++i) {
		musicTrack_t* track = trackAt(i, trackView);
		if (!track)
			continue;
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
 * NAME : configAction
 * DESCRIPTION : Save or load Music Player settings through the loader.
 * NOTES : Loaded defaults take effect on the current screen.
 * ARGS : Nonzero to save.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void configAction(void* saving)
{
	int count = sizeof(settings) / sizeof(settings[0]);
	int result = saving ? musicLoader->saveConfig("musicplayer", settings, count) :
	    musicLoader->loadConfig("musicplayer", settings, count);
	if (!saving && result == UYA_CONFIG_OK) {
		nativeMusic(0);
		selected = 0;
		previousView = trackView;
		requested = autoPlay;
		lastContext = -1;
	}
	musicText(status, result == UYA_CONFIG_OK ? (saving ? "Settings saved" : "Settings loaded") :
	    result == UYA_CONFIG_MISSING ? "No saved settings" : "Settings file error", 0, "");
}

/*
 * NAME : applyDefaultPlaylist
 * DESCRIPTION : Apply a screen's saved playlist once when entering it.
 * NOTES : Base restores native music; All and Custom start the chosen playlist.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void applyDefaultPlaylist(void)
{
	int context = musicBackendContext();
	int choice;
	if (context < 0 || context == lastContext)
		return;
	lastContext = context;
	choice = context == 0 ? defaultLobby : context == 1 ? defaultStaging :
	    context == 3 ? defaultCampaign : defaultGame;
	nativeMusic(0);
	playlist = choice;
	selected = 0;
	requested = choice != MUSIC_BASE;
	resumePlayback = requested;
	shuffleStart = requested && shufflePlaylist();
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
	modMenuItem_t items[] = {
		{"Playlist", MODMENU_RANGE, &playlist, MUSIC_BASE, MUSIC_CUSTOM_SHUFFLE, 1, changeSelection, (void*)1},
		{"Track", MODMENU_RANGE, &selected, 0, MUSIC_MAX_TRACKS * 2 - 1, 1, changeSelection, 0},
		{"", MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0},
		{"Track View", MODMENU_RANGE, &trackView, 0, 1, 1, changeView, 0},
		{"Refresh Music", MODMENU_ACTION, 0, 0, 0, 0, refreshCustom, 0},
		{"Default Playlists", MODMENU_HEADER, 0, 0, 0, 0, 0, 0},
		{"Campaign", MODMENU_RANGE, &defaultCampaign, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE, 1, 0, 0},
		{"Online Lobby", MODMENU_RANGE, &defaultLobby, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE, 1, 0, 0},
		{"Staging", MODMENU_RANGE, &defaultStaging, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE, 1, 0, 0},
		{"In Game", MODMENU_RANGE, &defaultGame, MUSIC_BASE, MUSIC_COMBINED_SHUFFLE, 1, 0, 0},
		{"", MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0}
	};
	static const modMenuButton_t configButtons[] = {
		{"Load Settings", configAction, 0},
		{"Save Settings", configAction, (void*)1}
	};
	int handles[11], i;
	menu = modmenuFind(musicLoader);
	if (!menu)
		return;
	tab = menu->addTab("musicplayer", "Music Player", UYA_MODE_BOTH);
	if (tab < 0) {
		tab = 0;
		return;
	}
	for (i = 0; i < 11; ++i) {
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
	menu->setItemValueLabel(handles[3], viewLabel);
	for (i = 6; i <= 9; ++i)
		menu->setItemValueLabel(handles[i], playlistLabel);
	menu->setItemButtons(handles[10], configButtons, 2);
	menu->setTabAction(tab, "Refresh Music", refreshCustom, 0);
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
	musicBackendStartup(0);
	nativeMusic(0);
	if (menu && tab)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	lastLevel = -2;
	lastContext = -1;
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
	/* Older settings used Playlist for campaign playback. */
	defaultCampaign = playlist;
	api->loadConfig("musicplayer", &settings[3], 1);
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
	int context = musicLoader->mode == UYA_MODE_CAMPAIGN ? 3 : 2;
	int choice = context == 3 ? defaultCampaign : defaultGame;
	int startup = musicBackendStartup(choice != MUSIC_BASE ||
	    (lastContext == context && playlist != MUSIC_BASE && (playing || requested || resumePlayback)));
	int ready = musicBackendReady();
	if (!ready) {
		/* Disc/custom playlists survive loading, but their old streams cannot. */
		if ((playing || requested) && playlist != MUSIC_BASE) {
			resumePlayback = 1;
			resumePaused = paused;
		}
		musicCustomClose();
		customPlaying = 0;
		musicBackendRelease(0);
		playing = paused = requested = stopped = 0;
		lastLevel = -2;
		return;
	}
	if (lastLevel != musicBackendLevel() || lastMode != (int)musicLoader->mode) {
		int resume = resumePlayback || (playlist != MUSIC_BASE && (playing || requested));
		int wasPaused = resumePaused || paused;
		int track = selected;
		nativeMusic(0);
		lastLevel = musicBackendLevel();
		if (lastMode != (int)musicLoader->mode || (context == 3 && !resume))
			lastContext = -1;
		lastMode = musicLoader->mode;
		selected = resume ? track : 0;
		captureBase();
		requested = resume || autoPlay;
		resumePlayback = resume;
		resumePaused = resume && wasPaused;
		shuffleStart = resume && shufflePlaylist() && musicBackendContext() >= 2;
	}
	if (startup && !requested && !playing && !resumePlayback)
		lastContext = -1;
	applyDefaultPlaylist();
	if (!tab)
		registerMenu();
	musicBackendControl();
	if (stopped)
		musicBackendSilence();
	/* A saved Custom selection must not start the helper during boot
	 * unless playback or the open Music Player tab actually needs it. */
	if (!customScanned && ((includesCustom(playlist) &&
	    (requested || playing || (tab && menu->isOpen() && menu->getActiveTab() == tab))) ||
	    (musicLoader->mode == UYA_MODE_MULTIPLAYER && musicBackendLevel() == 39 &&
	    includesCustom(defaultGame)))) {
		int result = musicCustomScan();
		if (result <= 0)
			musicText(status, result < 0 ? "Cannot scan uya/music" : "Reading custom songs", 0, "");
		if (result)
			customScanned = 1;
		if (result > 0)
			musicText(status, "Custom songs ready", 0, "");
	}
	if (catalogueGroup < (int)(sizeof(groups) / sizeof(groups[0])) &&
	    (includesGame(playlist) ||
	    (musicLoader->mode == UYA_MODE_MULTIPLAYER && musicBackendLevel() == 39 &&
	    includesGame(defaultGame)))) {
		musicText(status, "Reading disc music", catalogueGroup + 1, "");
		addDiscGroup(catalogueGroup++);
		if (catalogueGroup == (int)(sizeof(groups) / sizeof(groups[0])))
			musicText(status, "Disc songs ready", 0, "");
		if (includesGame(playlist))
			return;
	}
	if (includesCustom(playlist) && !customScanned)
		return;
	chooseShuffleStart();
	if (selected >= trackCount() && trackCount())
		selected = 0;
	followNativeTrack();
	/* Let retail establish the new sound stream before taking it over. The
	 * overlay and scene-ready flag can become valid before audio setup ends. */
	if (resumePlayback) {
		musicStream_t* stream = musicBackendStream();
		/* Retail can leave its handle at -1 while a per-frame query is
		 * pending. States 4/8 establish readiness; Silence drains that query. */
		if (!startup && stream->status != 4 && stream->status != 8)
			return;
		resumePlayback = 0;
	}
	if (requested && trackCount() && (!includesCustom(playlist) || customScanned) && musicBackendAcquire()) {
		int result;
		musicCustomClose();
		customPlaying = 0;
		if (customTrackIndex(selected) >= 0)
			result = musicBackendSilence() ? musicCustomOpen(customTrackIndex(selected)) : 0;
		else {
			musicTrack_t* track = trackAt(selected, trackView);
			const musicTrack_t* body = !trackView && track->part == MUSIC_INTRO ? track + 1 : 0;
			/* Loop Track continuation retained for later:
			 * if (repeatBody && body) {
			 *     track = (musicTrack_t*)body;
			 *     body = 0;
			 * }
			 */
			result = musicBackendSilence() ? musicDiscOpen(track, body, 0) : 0;
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
