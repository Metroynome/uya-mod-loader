#include "platform.h"
/* Resident SDK functions: campaign boot core1 uses base 0x116f80;
 * multiplayer uses the resident layout in the supplied NTSC-U EE dump.
 * Regional validation and provenance are documented in ADDRESSES.md.
 * Keep these separate from librac3/libuya so their shared symbol names cannot
 * cause the loader to bind to the wrong game mode.
 */
void platformApi(ModuleApi *api, unsigned int mode)
{
    int sp = mode == UYA_MODE_CAMPAIGN;
    api->version = UYA_MODULE_VERSION;
    api->region = UYA_MODULE_REGION;
    api->mode = mode;
    api->print = (void *)(sp ? 0x00121760 : 0x00128c70);
    api->open = (void *)(sp ? 0x00123430 : 0x0012aa38);
    api->close = (void *)(sp ? 0x001236c0 : 0x0012acc8);
    api->read = (void *)(sp ? 0x00123840 : 0x0012b080);
    api->flushCache = (void *)(sp ? 0x0011f0a0 : 0x001264e0);
}

/* Refuse unknown SDK layouts instead of calling an address from another build.
 * This also defers loading while the resident core is being replaced. */
int platformReady(unsigned int mode)
{
    int sp = mode == UYA_MODE_CAMPAIGN;
    if (mode != UYA_MODE_CAMPAIGN && mode != UYA_MODE_MULTIPLAYER)
        return 0;
    return *(volatile unsigned int *)(sp ? 0x121760 : 0x128c70) == 0x27bdff40 &&
           *(volatile unsigned int *)(sp ? 0x123430 : 0x12aa38) == 0x27bdfeb0 &&
           *(volatile unsigned int *)(sp ? 0x1236c0 : 0x12acc8) == 0x27bdff60 &&
           *(volatile unsigned int *)(sp ? 0x123840 : 0x12b080) == 0x27bdff20 &&
           *(volatile unsigned int *)(sp ? 0x11f0a0 : 0x1264e0) == 0x24030064;
}

/* Campaign level -1 is the initial main menu and may run campaign modules.
 * Level 0 is a pre-load state; other negative IDs are not recognized.
 * The lobby is 0x27; higher IDs (including local MP variants) are multiplayer.
 * Only read the global belonging to the verified resident SDK layout. */
unsigned int platformMode(unsigned int layout)
{
    int level;
    unsigned int mode;
    if (layout == UYA_MODE_CAMPAIGN) {
#if UYA_MODULE_REGION == 3
        level = *(volatile int *)0x001de75c;
#else
        level = *(volatile int *)0x001d545c;
#endif
    } else if (layout == UYA_MODE_MULTIPLAYER) {
#if UYA_MODULE_REGION == 2
        level = *(volatile int *)0x001f83a8;
#elif UYA_MODULE_REGION == 1
        level = *(volatile int *)0x001f8528;
#else
        /* No verified NTSC-J multiplayer global; never use NTSC-U's address. */
        return 0;
#endif
    } else {
        return 0;
    }
    /* The boot menu uses the campaign SDK, even though no planet is loaded.
     * Do not apply this exception to the multiplayer pre-load layout. */
    if (layout == UYA_MODE_CAMPAIGN && level == -1)
        return UYA_MODE_CAMPAIGN;
    if (level <= 0)
        return 0;
    mode = level < 0x27 ? UYA_MODE_CAMPAIGN : UYA_MODE_MULTIPLAYER;
    /* A new level ID can appear before the replacement core is ready. */
    return mode == layout ? mode : 0;
}
