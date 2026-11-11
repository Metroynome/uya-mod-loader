#include "decoder.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

static mp3dec_t decoder;
static mp3dec_frame_info_t info;
static mp3d_sample_t samples[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int available, position;

/*
 * NAME : nextFrame
 * DESCRIPTION : Decode the next MPEG Layer III frame.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int nextFrame(void)
{
	const u8* data;
	int bytes, attempts;
	for (attempts = 0; attempts < 16; ++attempts) {
		bytes = musicInputPeek(&data);
		if (bytes <= 0)
			return bytes;
		available = mp3dec_decode_frame(&decoder, data, bytes, samples, &info);
		musicInputConsume(info.frame_bytes);
		position = 0;
		if (available)
			return available;
		if (!info.frame_bytes)
			return -1;
	}
	return -1;
}

/*
 * NAME : openMp3
 * DESCRIPTION : Skip supported ID3 metadata and initialize MP3 playback.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int openMp3(musicFormat_t* format)
{
	const u8* data;
	int bytes = musicInputPeek(&data);
	if (bytes < 0)
		return 0;
	if (bytes >= 10 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
		u32 remaining = ((u32)(data[6] & 127) << 21) | ((u32)(data[7] & 127) << 14) |
		    ((u32)(data[8] & 127) << 7) | (data[9] & 127);
		u8 ignored[256];
		if (remaining > 1048576)
			return 0;
		remaining += 10 + ((data[5] & 16) ? 10 : 0);
		while (remaining) {
			u32 part = remaining > sizeof(ignored) ? sizeof(ignored) : remaining;
			if (musicInputRead(ignored, part) != (int)part)
				return 0;
			remaining -= part;
		}
	}
	mp3dec_init(&decoder);
	position = available = 0;
	if (nextFrame() <= 0)
		return 0;
	format->rate = info.hz;
	format->channels = info.channels;
	format->frames = 0;
	return 1;
}

/*
 * NAME : readMp3
 * DESCRIPTION : Return decoded PCM frames while preserving frame leftovers.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int readMp3(s16* pcm, int frames)
{
	int n, channel, rate = info.hz, channels = info.channels;
	for (n = 0; n < frames; ++n) {
		if (position == available) {
			int result = nextFrame();
			if (result < 0)
				return -1;
			if (!result)
				break;
			if (info.hz != rate || info.channels != channels)
				return -1;
		}
		for (channel = 0; channel < channels; ++channel)
			pcm[n * channels + channel] = samples[position * channels + channel];
		++position;
	}
	return n;
}

const musicDecoder_t musicMp3Decoder = {openMp3, readMp3};
