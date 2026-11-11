#ifndef UYA_MUSIC_DECODER_H
#define UYA_MUSIC_DECODER_H

#include "../musicplayer.h"

typedef struct musicFormat {
	/* 0x00 */ u32 rate;
	/* 0x04 */ u32 frames;
	/* 0x08 */ u32 channels;
} musicFormat_t;

typedef struct musicDecoder {
	/* 0x00 */ int (*open)(musicFormat_t* format);
	/* 0x04 */ int (*read)(s16* pcm, int frames);
} musicDecoder_t;

extern const musicDecoder_t musicWavDecoder;
extern const musicDecoder_t musicMp3Decoder;
extern const musicDecoder_t musicVagDecoder;

int musicInputRead(void* buffer, int bytes);
int musicInputPeek(const u8** bytes);
void musicInputConsume(int bytes);
int musicDecodeOpen(const char* path, musicFormat_t* format);
int musicDecodeRead(s16* pcm, int frames);
void musicDecodeClose(void);

#endif
