#ifndef LOADER_PLATFORM_H
#define LOADER_PLATFORM_H

#include "module.h"

/* A hook identifies an SDK layout, not necessarily a playable game mode. */
u32 platformMode(u32 layout);
u32 platformContext(u32 mode);
u32 platformMemorySize(void);
int platformReady(u32 mode);
void platformApi(ModuleApi_t* api, u32 mode);
#endif
