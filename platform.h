#ifndef LOADER_PLATFORM_H
#define LOADER_PLATFORM_H
#include "module.h"
/* A hook identifies an SDK layout, not necessarily a playable game mode. */
unsigned int platformMode(unsigned int layout);
int platformReady(unsigned int mode);
void platformApi(ModuleApi *api, unsigned int mode);
#endif
