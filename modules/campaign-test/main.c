#include "module.h"
#include <librac3/stdio.h>
/* Include <librac3/...h> for this mode's game functions. */

/* This pointer is updated by the loader on mode transitions. */
static const ModuleApi *loader;
int moduleInit(const ModuleApi *api)
{
    loader = api;
    printf("campaign-test: initialized (region %u, mode %u)\n", api->region, api->mode);
    return 0;
}
void moduleUpdate(void)
{
    /* Called only in this module's declared mode. Check that the level/player
     * is ready before using game-library addresses. Keep persistent state here
     * in module BSS, never in hard-coded game memory. */
}
