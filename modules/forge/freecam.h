#ifndef FORGE_FREECAM_H
#define FORGE_FREECAM_H

#include "module.h"

/* Saved preferences; active camera state and bookmarks remain runtime-only. */
typedef struct freecamSettings {
	/* 0x00 */ int enabled;
	/* 0x04 */ int airwalk;
	/* 0x08 */ int lockPosition;
	/* 0x0c */ int lockAnimation;
} freecamSettings_t;

extern freecamSettings_t forgeFreecam;

void forgeFreecamInit(const moduleApi_t* api);
int forgeFreecamAvailable(void);
void forgeFreecamUpdate(int menuOpen, int loading);
void forgeFreecamReset(void);

#endif
