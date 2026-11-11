#include "decoder.h"

static int descriptor = -1, position, length, ended;
static u32 fileMode;
static u8 input[8192];
static const musicDecoder_t* active;

/*
 * NAME : musicInputPeek
 * DESCRIPTION : Refill the bounded input buffer without discarding unread bytes.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicInputPeek(const u8** bytes)
{
	int i, count;
	if (position) {
		for (i = position; i < length; ++i)
			input[i - position] = input[i];
		length -= position;
		position = 0;
	}
	if (!ended && length < (int)sizeof(input)) {
		count = musicLoader->read(descriptor, input + length, sizeof(input) - length);
		if (count < 0)
			return -1;
		length += count;
		ended = count == 0;
	}
	*bytes = input;
	return length;
}

/*
 * NAME : musicInputConsume
 * DESCRIPTION : Advance past compressed bytes consumed by a decoder.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicInputConsume(int bytes)
{
	position += bytes;
}

/*
 * NAME : musicInputRead
 * DESCRIPTION : Read input bytes across buffer boundaries.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicInputRead(void* buffer, int bytes)
{
	u8* out = buffer;
	int total = 0;
	while (total < bytes) {
		const u8* data;
		int count = length - position;
		if (!count) {
			count = musicInputPeek(&data);
			if (count < 0)
				return -1;
			if (!count)
				break;
		}
		if (count > bytes - total)
			count = bytes - total;
		int i;
		for (i = 0; i < count; ++i)
			out[total + i] = input[position + i];
		position += count;
		total += count;
	}
	return total;
}

/*
 * NAME : musicDecodeOpen
 * DESCRIPTION : Choose a decoder from the file header.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicDecodeOpen(const char* path, musicFormat_t* format)
{
	const u8* data;
	musicDecodeClose();
	descriptor = musicLoader->open(path, 1, 0);
	fileMode = musicLoader->mode;
	if (descriptor < 0 || musicInputPeek(&data) < 12)
		goto failed;
	if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F')
		active = &musicWavDecoder;
	else if (data[0] == 'V' && data[1] == 'A' && data[2] == 'G' && data[3] == 'p')
		active = &musicVagDecoder;
	else
		active = &musicMp3Decoder;
	if (active->open(format) && format->channels > 0 && format->channels <= 2 &&
	    format->rate >= 8000 && format->rate <= 48000)
		return 1;
failed:
	musicDecodeClose();
	return 0;
}

/*
 * NAME : musicDecodeRead
 * DESCRIPTION : Decode interleaved PCM frames.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int musicDecodeRead(s16* pcm, int frames)
{
	return active ? active->read(pcm, frames) : -1;
}

/*
 * NAME : musicDecodeClose
 * DESCRIPTION : Close input without using a stale ELF's descriptor.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void musicDecodeClose(void)
{
	if (descriptor >= 0 && fileMode == musicLoader->mode)
		musicLoader->close(descriptor);
	descriptor = -1;
	active = 0;
	position = length = ended = 0;
}
