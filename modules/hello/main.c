#include "module.h"

/* This pointer is updated by the loader on mode transitions. */
static const moduleApi_t* loader;
static u32 updates;

int moduleInit(const moduleApi_t* api)
{
	loader = api;
	loader->print("hello: initialized (region %u, mode %u)\n", api->region, api->mode);
	return 0;
}

void moduleUpdate(void)
{
	/* Match the SM example: report progress every 300 loader callbacks.
     * These are hook calls, not necessarily video frames. */
	if (++updates % 300 == 0)
		loader->print("hello: %u updates\n", updates);
}
