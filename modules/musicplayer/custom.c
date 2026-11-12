#include "musicplayer.h"
#include "modules/forge/include/forge.h"
#include "decoders/decoder.h"
#include <sifrpc.h>
#include <string.h>

#define MUSIC_RING_BYTES (65536)
#define MUSIC_CAMPAIGN_RING_BYTES (131072)
#define MUSIC_REFILL_BYTES (1024)
#define MUSIC_EE_STREAM (0x4000)
#define MUSIC_STREAM_LENGTH (0x10000000)
#define UNCACHED(pointer) ((void*)((u32)(pointer) | 0x20000000))

typedef struct musicMonitor {
	/* 0x00 */ u32 bufferSize;
	/* 0x04 */ u32 start;
	/* 0x08 */ u32 next;
	/* 0x0c */ u32 consumed;
	/* 0x10 */ u8 padding[48];
} musicMonitor_t;

static SifRpcClientData_t client __attribute__((aligned(64)));
static u32 request[128] __attribute__((aligned(64)));
static u8 reply[320] __attribute__((aligned(64)));
static char filenames[MUSIC_MAX_TRACKS][96];
static int fileCount, scanState, directory = -1, scanMode, scanRequested = 1;
static musicMonitor_t monitors[2] __attribute__((aligned(64)));
static u8 rings[2][MUSIC_CAMPAIGN_RING_BYTES] __attribute__((aligned(64)));
static u32 ringBytes;
static musicFormat_t format;
static u32 handle, produced, framesDecoded, initialRemaining, elapsed;
static int streamPending, opened, ending, remainingSet, paused, closing, openedMode;
static u32 generation;
static int discStream, discEnded;
static u32 discBody[2];
static int discQueue;
static int discLoop;

/*
 * NAME : rpcBusy
 * DESCRIPTION : Check whether the directory RPC is still pending.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int rpcBusy(void)
{
	u32 address = musicLoader->mode == UYA_MODE_CAMPAIGN ? 0x122a10 : 0x12a018;
	return ((int (*)(SifRpcClientData_t*))address)(&client);
}

/*
 * NAME : rpcRequest
 * DESCRIPTION : Send a nonblocking directory-helper request.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int rpcRequest(int command, int sendSize, int receiveSize)
{
	u32 address = musicLoader->mode == UYA_MODE_CAMPAIGN ? 0x122810 : 0x129e18;
	musicLoader->flushCache(0);
	return ((int (*)(SifRpcClientData_t*, int, int, void*, int, void*, int, void*, void*))address)(
	    &client, command, 1, request, sendSize, reply, receiveSize, 0, 0) >= 0;
}

/*
 * NAME : supported
 * DESCRIPTION : Accept supported audio filenames that fit the catalogue.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int supported(const char* name)
{
	int n = 0;
	char extension[4];
	while (name[n]) {
		if (n >= 95 || name[n] == ':' || name[n] == '/' || name[n] == '\\')
			return 0;
		++n;
	}
	if (n < 5 || name[n - 4] != '.' || name[0] == '.')
		return 0;
	int i;
	for (i = 0; i < 3; ++i) {
		char c = name[n - 3 + i];
		extension[i] = c >= 'A' && c <= 'Z' ? c + 32 : c;
	}
	return (extension[0] == 'w' && extension[1] == 'a' && extension[2] == 'v') ||
	    (extension[0] == 'm' && extension[1] == 'p' && extension[2] == '3') ||
	    (extension[0] == 'v' && extension[1] == 'a' && extension[2] == 'g');
}

/*
 * NAME : musicCustomRefresh
 * DESCRIPTION : Request custom-song discovery again.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicCustomRefresh(void)
{
	scanRequested = 1;
}

/*
 * NAME : bindDirectory
 * DESCRIPTION : Bind to the current core's native RPC client.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int bindDirectory(u32 service)
{
	u32 address = musicLoader->mode == UYA_MODE_CAMPAIGN ? 0x122630 : 0x129c38;
	memset(&client, 0, sizeof(client));
	return ((int (*)(SifRpcClientData_t*, int, int))address)(&client, service, 1) >= 0;
}

/*
 * NAME : musicCustomScan
 * DESCRIPTION : Discover songs through the shared IOP directory helper.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicCustomScan(void)
{
	const mapHopApi_t* maps = musicLoader->findService(MAP_HOP_SERVICE_ID, FORGE_API_VERSION);
	int result;
	if (maps && maps->state() == FORGE_SCANNING)
		return 0;
	if (scanMode != (int)musicLoader->mode) {
		scanMode = musicLoader->mode;
		scanState = 0;
		directory = -1;
		scanRequested = 1;
	}
	if (!scanRequested && !scanState)
		return 1;
	if (scanState && rpcBusy())
		return 0;
	result = *(volatile int*)UNCACHED(reply);
	if (!scanState) {
		if (!bindDirectory(0x554d5553))
			goto failed;
		scanRequested = 0;
		fileCount = 0;
		scanState = 1;
		return 0;
	}
	if (scanState == 1) {
		if (!client.server) {
			/* Map Loader owns helper startup when present. Loading another
			 * copy while its first scan binds can strand either RPC client. */
			if (musicLoader->findService(MAP_HOP_SERVICE_ID, FORGE_API_VERSION)) {
				scanState = 0;
				scanRequested = 1;
				return 0;
			}
			if (!bindDirectory(0x80000006))
				goto failed;
			scanState = 5;
			return 0;
		}
		if (!rpcRequest(1, 64, sizeof(reply)))
			goto failed;
		scanState = 2;
		return 0;
	}
	if (scanState == 5) {
		const char* path = "host0:uya/mods/utils/mapscan.irx";
		int i;
		if (!client.server)
			goto failed;
		memset(request, 0, sizeof(request));
		for (i = 0; path[i]; ++i)
			((char*)request)[8 + i] = path[i];
		if (!rpcRequest(0, sizeof(request), 8))
			goto failed;
		scanState = 6;
		return 0;
	}
	if (scanState == 6) {
		if (result < 0 || !bindDirectory(0x554d5553))
			goto failed;
		scanState = 7;
		return 0;
	}
	if (scanState == 7) {
		if (!client.server) {
			if (++directory > 120 || !bindDirectory(0x554d5553))
				goto failed;
			return 0;
		}
		scanState = 1;
		return 0;
	}
	if (scanState == 2) {
		if (result < 0)
			goto failed;
	} else if (scanState == 3) {
		if (result > 0 && fileCount < MUSIC_MAX_TRACKS) {
			const char* found = (const char*)UNCACHED(reply) + 4;
			if (supported(found)) {
				int i;
				for (i = 0; i < 96; ++i)
					filenames[fileCount][i] = found[i];
				++fileCount;
			}
		}
		if (result <= 0 || fileCount == MUSIC_MAX_TRACKS) {
			if (!rpcRequest(3, 64, sizeof(reply)))
				goto failed;
			scanState = 4;
			return 0;
		}
	} else if (scanState == 4) {
		scanState = 0;
		directory = -1;
		return 1;
	}
	if (!rpcRequest(2, 64, sizeof(reply)))
		goto failed;
	scanState = 3;
	return 0;
failed:
	scanState = 0;
	scanRequested = 0;
	return -1;
}

/*
 * NAME : musicCustomCount
 * DESCRIPTION : Return the discovered custom-song count.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicCustomCount(void)
{
	return fileCount;
}

/*
 * NAME : musicCustomName
 * DESCRIPTION : Return a custom-song filename.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* musicCustomName(int index)
{
	return index >= 0 && index < fileCount ? filenames[index] : "No custom songs";
}

/*
 * NAME : streamStarted
 * DESCRIPTION : Accept the native stream handle for the current request.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void streamStarted(int result, u64 context)
{
	if ((u32)context != generation)
		return;
	handle = result;
	streamPending = 0;
	if (!opened)
		musicCustomClose();
}

/*
 * NAME : streamRemaining
 * DESCRIPTION : Track playback time using the native sound engine.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void streamRemaining(int result, u64 context)
{
	if ((u32)context != generation)
		return;
	if (result <= 0) {
		if (discStream && remainingSet && !discLoop)
			discEnded = 1;
		streamPending = 0;
		return;
	}
	if (!remainingSet || (u32)result > initialRemaining) {
		initialRemaining = result;
		remainingSet = 1;
	}
	elapsed = initialRemaining - (u32)result;
	streamPending = 0;
}

/*
 * NAME : writeBig
 * DESCRIPTION : Write a big-endian VAG header field.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void writeBig(u8* output, u32 value)
{
	output[0] = value >> 24;
	output[1] = value >> 16;
	output[2] = value >> 8;
	output[3] = value;
}

/* Each block carries 28 samples. Filter zero keeps the encoder bounded and
 * independent between blocks; SPU2 performs the ADPCM decode and mixing. */
/*
 * NAME : encodeBlock
 * DESCRIPTION : Encode 28 PCM samples as one PS2 ADPCM block.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void encodeBlock(u8* output, const s16* pcm, int stride, int final)
{
	int i, shift = 0, maximum = 0;
	for (i = 0; i < 28; ++i) {
		int value = pcm[i * stride];
		if (value < 0)
			value = -value;
		if (value > maximum)
			maximum = value;
	}
	while (shift < 12 && maximum <= (7 << (11 - shift)))
		++shift;
	output[0] = shift;
	output[1] = final ? 1 : 0;
	for (i = 0; i < 14; ++i) {
		int low = ((int)pcm[i * 2 * stride] * (1 << shift) + 2048) >> 12;
		int high = ((int)pcm[(i * 2 + 1) * stride] * (1 << shift) + 2048) >> 12;
		if (low < -8)
			low = -8;
		if (low > 7)
			low = 7;
		if (high < -8)
			high = -8;
		if (high > 7)
			high = 7;
		output[i + 2] = (low & 15) | ((high & 15) << 4);
	}
}

/*
 * NAME : pump
 * DESCRIPTION : Refill only ring-buffer space already consumed by the IOP.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int pump(u32 bytes)
{
	s16 pcm[56];
	while (bytes >= 16) {
		int channel, frames = ending ? 0 : musicDecodeRead(pcm, 28);
		if (frames < 0)
			return 0;
		framesDecoded += frames;
		if (frames < 28) {
			int i;
			ending = 1;
			for (i = frames * format.channels; i < (int)(28 * format.channels); ++i)
				pcm[i] = 0;
		}
		for (channel = 0; channel < (int)format.channels; ++channel) {
			u8* target = (u8*)UNCACHED(rings[channel]) + (produced % ringBytes);
			encodeBlock(target, pcm + channel, format.channels, ending);
		}
		produced += 16;
		bytes -= 16;
	}
	return 1;
}

/*
 * NAME : musicCustomOpen
 * DESCRIPTION : Decode and prebuffer a selected custom song.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicCustomOpen(int index)
{
	char path[128];
	const char* prefix = MUSIC_CUSTOM_PATH;
	const char* name = musicCustomName(index);
	u32 data[8];
	int channel, i = 0;
	if (opened || handle || streamPending || closing) {
		musicCustomClose();
		return 0;
	}
	while (*prefix)
		path[i++] = *prefix++;
	while (*name && i < (int)sizeof(path) - 1)
		path[i++] = *name++;
	path[i] = 0;
	if (!musicDecodeOpen(path, &format))
		return -1;
	ringBytes = musicLoader->mode == UYA_MODE_CAMPAIGN ? MUSIC_CAMPAIGN_RING_BYTES : MUSIC_RING_BYTES;
	discStream = discEnded = 0;
	ending = paused = remainingSet = 0;
	framesDecoded = elapsed = 0;
	produced = 48;
	musicLoader->flushCache(0);
	for (channel = 0; channel < (int)format.channels; ++channel) {
		u8* bytes = UNCACHED(rings[channel]);
		memset(bytes, 0, ringBytes);
		bytes[0] = 'V';
		bytes[1] = 'A';
		bytes[2] = 'G';
		bytes[3] = 'p';
		writeBig(bytes + 4, 0x20);
		writeBig(bytes + 12, MUSIC_STREAM_LENGTH);
		writeBig(bytes + 16, format.rate);
		musicMonitor_t* monitor = UNCACHED(&monitors[channel]);
		monitor->bufferSize = ringBytes;
		monitor->start = (u32)rings[channel];
		monitor->next = (u32)rings[channel];
		monitor->consumed = 0;
	}
	if (!pump(ringBytes - 48)) {
		musicDecodeClose();
		return -1;
	}
	data[0] = (u32)&monitors[0];
	data[1] = format.channels == 2 ? (u32)&monitors[1] : 0;
	data[2] = 1024 << 16;
	data[3] = 0;
	data[4] = 1;
	data[5] = data[6] = 0;
	data[7] = MUSIC_EE_STREAM;
	streamPending = opened = 1;
	openedMode = musicLoader->mode;
	++generation;
	musicBackendCommand(0x2c, sizeof(data), data, streamStarted, generation);
	return 1;
}

/*
 * NAME : musicDiscOpen
 * DESCRIPTION : Play a disc song without retail's automatic native queue.
 * NOTES : Uses the same sound-driver handle and controls as custom audio.
 * ARGS : Absolute stereo channel descriptors.
 * RETURN : One when started, zero while the previous stream closes.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicDiscOpen(const musicTrack_t* track, const musicTrack_t* body, int loop)
{
	u32 data[8] = {track->channels[0], track->channels[2], 1024 << 16, 0, 1, 0, 0, body ? 0 : loop ? 4 : 0};
	if (opened || handle || streamPending || closing) {
		musicCustomClose();
		return 0;
	}
	discStream = 1;
	discLoop = loop;
	discQueue = body != 0;
	if (body) {
		discBody[0] = body->channels[0];
		discBody[1] = body->channels[2];
	}
	discEnded = paused = remainingSet = 0;
	elapsed = 0;
	streamPending = opened = 1;
	openedMode = musicLoader->mode;
	++generation;
	musicBackendCommand(0x2c, sizeof(data), data, streamStarted, generation);
	return 1;
}

/*
 * NAME : discQueued
 * DESCRIPTION : Acknowledge a prebuffered loop body on the current stream.
 * NOTES : Queueing returns a result code, not a replacement stream handle.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void discQueued(int result, u64 context)
{
	if ((u32)context != generation)
		return;
	discQueue = result ? 0 : -1;
	streamPending = 0;
}

/*
 * NAME : musicCustomTick
 * DESCRIPTION : Refill custom audio and detect completed playback.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicCustomTick(void)
{
	u32 consumed, freeBytes;
	int channel;
	if (!opened)
		return -1;
	/* Startup and timing-query callbacks must not stall the EE producer. */
	if (!discStream) {
		consumed = ((volatile musicMonitor_t*)UNCACHED(&monitors[0]))->consumed;
		for (channel = 1; channel < (int)format.channels; ++channel) {
			u32 other = ((volatile musicMonitor_t*)UNCACHED(&monitors[channel]))->consumed;
			if (other < consumed)
				consumed = other;
		}
		freeBytes = consumed - (produced - ringBytes);
		if (freeBytes > ringBytes)
			return -1; /* Stop on underrun rather than replay overwritten audio. */
		if (freeBytes > MUSIC_REFILL_BYTES)
			freeBytes = MUSIC_REFILL_BYTES;
		if (!pump(freeBytes))
			return -1;
	}
	if (streamPending)
		return 0;
	if (!handle)
		return -1;
	if (discStream) {
		if (discQueue < 0)
			return -1;
		if (discQueue) {
			/* Campaign continuation reverses the channel descriptors and uses
			 * flag 4 only when the body should loop, rather than MP flag 8. */
			u32 data[8] = {discBody[1], discBody[0], 1024 << 16, 0, 1, handle, 0, discLoop ? 4 : 0};
			streamPending = 1;
			musicBackendCommand(0x2c, sizeof(data), data, discQueued, generation);
			return 0;
		}
		if (discEnded)
			return 1;
		if (!paused) {
			streamPending = 1;
			musicBackendCommand(0x32, 4, &handle, streamRemaining, generation);
		}
		return 0;
	}
	if (ending && remainingSet && elapsed >= ((u64)framesDecoded * 1000) / format.rate)
		return 1;
	if (!paused) {
		streamPending = 1;
		musicBackendCommand(0x32, 4, &handle, streamRemaining, generation);
	}
	return 0;
}

/*
 * NAME : musicCustomPause
 * DESCRIPTION : Pause or resume only the custom music stream.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicCustomPause(int value)
{
	paused = value;
	if (handle)
		musicBackendCommand(value ? 0x2d : 0x2e, 4, &handle, 0, 0);
}

/*
 * NAME : streamClosed
 * DESCRIPTION : Acknowledge the end of the current custom stream.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void streamClosed(int result, u64 context)
{
	(void)result;
	if ((u32)context != generation)
		return;
	handle = 0;
	closing = 0;
}

/*
 * NAME : musicCustomClose
 * DESCRIPTION : Stop custom playback before reusing its buffers.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicCustomClose(void)
{
	opened = 0;
	musicDecodeClose();
	if (openedMode != (int)musicLoader->mode) {
		++generation;
		handle = 0;
		streamPending = closing = 0;
	} else if (handle && !closing) {
		closing = 1;
		musicBackendCommand(0x2f, 4, &handle, streamClosed, generation);
	}
}
