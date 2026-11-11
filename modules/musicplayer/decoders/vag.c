#include "decoder.h"

static s16 samples[28];
static int position, available, history[2];
static u32 remaining;
static const int coefficients[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/*
 * NAME : big
 * DESCRIPTION : Read a big-endian VAG header field.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 big(const u8* data)
{
	return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

/*
 * NAME : openVag
 * DESCRIPTION : Validate a mono VAGp stream.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int openVag(musicFormat_t* format)
{
	u8 header[48];
	if (musicInputRead(header, sizeof(header)) != sizeof(header))
		return 0;
	remaining = big(header + 12);
	format->rate = big(header + 16);
	format->channels = 1;
	format->frames = remaining / 16 * 28;
	position = available = history[0] = history[1] = 0;
	return remaining && !(remaining & 15);
}

/*
 * NAME : readVag
 * DESCRIPTION : Decode PS2 ADPCM predictor blocks into PCM.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int readVag(s16* pcm, int frames)
{
	int n;
	for (n = 0; n < frames; ++n) {
		if (position == available) {
			u8 block[16];
			int i, predictor, shift;
			if (!remaining)
				break;
			if (musicInputRead(block, sizeof(block)) != sizeof(block))
				return -1;
			remaining -= 16;
			predictor = block[0] >> 4;
			shift = block[0] & 15;
			if (predictor > 4 || shift > 12)
				return -1;
			for (i = 0; i < 28; ++i) {
				int value = (block[2 + i / 2] >> ((i & 1) * 4)) & 15;
				value = ((value >= 8 ? value - 16 : value) * 4096) >> shift;
				value += (history[0] * coefficients[predictor][0] + history[1] * coefficients[predictor][1] + 32) >> 6;
				if (value > 32767)
					value = 32767;
				if (value < -32768)
					value = -32768;
				history[1] = history[0];
				history[0] = value;
				samples[i] = value;
			}
			position = 0;
			available = 28;
		}
		pcm[n] = samples[position++];
	}
	return n;
}

const musicDecoder_t musicVagDecoder = {openVag, readVag};
