#include "decoder.h"

static int channels, bits;
static u32 remaining;

/*
 * NAME : little
 * DESCRIPTION : Read a little-endian integer from a WAV chunk.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 little(const u8* bytes, int count)
{
	u32 value = 0;
	while (count--)
		value = (value << 8) | bytes[count];
	return value;
}

/*
 * NAME : openWav
 * DESCRIPTION : Validate PCM WAV metadata and locate audio data.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int openWav(musicFormat_t* format)
{
	u8 chunk[32];
	int found = 0, iterations;
	if (musicInputRead(chunk, 12) != 12 || chunk[8] != 'W' || chunk[9] != 'A' ||
	    chunk[10] != 'V' || chunk[11] != 'E')
		return 0;
	for (iterations = 0; iterations < 64; ++iterations) {
		u32 bytes, original;
		if (musicInputRead(chunk, 8) != 8)
			return 0;
		bytes = original = little(chunk + 4, 4);
		if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't' && chunk[3] == ' ') {
			if (bytes < 16 || musicInputRead(chunk, 16) != 16 || little(chunk, 2) != 1)
				return 0;
			channels = little(chunk + 2, 2);
			bits = little(chunk + 14, 2);
			if (channels < 1 || channels > 2 || (bits != 8 && bits != 16 && bits != 24 && bits != 32))
				return 0;
			format->rate = little(chunk + 4, 4);
			format->channels = channels;
			bytes -= 16;
			found = 1;
		} else if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' && chunk[3] == 'a') {
			if (!found)
				return 0;
			remaining = bytes;
			format->frames = bytes / (channels * bits / 8);
			return 1;
		}
		bytes += original & 1;
		/* Bound metadata skipping so malformed files cannot stall gameplay. */
		if (bytes > 1048576)
			return 0;
		while (bytes) {
			u32 part = bytes > sizeof(chunk) ? sizeof(chunk) : bytes;
			if (musicInputRead(chunk, part) != (int)part)
				return 0;
			bytes -= part;
		}
	}
	return 0;
}

/*
 * NAME : readWav
 * DESCRIPTION : Convert integer WAV samples to signed 16-bit PCM.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int readWav(s16* pcm, int frames)
{
	u8 data[8];
	int i, channel, bytes = bits / 8;
	for (i = 0; i < frames && remaining >= (u32)(bytes * channels); ++i) {
		if (musicInputRead(data, bytes * channels) != bytes * channels)
			return -1;
		for (channel = 0; channel < channels; ++channel) {
			u32 value = little(data + channel * bytes, bytes);
			pcm[i * channels + channel] = bits == 8 ? ((int)value - 128) * 256 :
			    bits == 16 ? (s16)value : (s32)(value << (32 - bits)) >> 16;
		}
		remaining -= bytes * channels;
	}
	return i;
}

const musicDecoder_t musicWavDecoder = {openWav, readWav};
