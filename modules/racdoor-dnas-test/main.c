#include "module.h"

typedef struct dnasTestState {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 size;
	/* 0x08 */ u32 initCount;
	/* 0x0c */ u32 campaignTicks;
	/* 0x10 */ u32 multiplayerTicks;
	/* 0x14 */ u32 attempts;
	/* 0x18 */ u32 applied;
	/* 0x1c */ u32 lastResult;
	/* 0x20 */ u32 mode;
	/* 0x24 */ u32 context;
	/* 0x28 */ u32 level;
} dnasTestState_t;

extern u32 dnasPayloadStart(void);

/* All fields are 32-bit EE words; discovery points directly to this table. */
volatile dnasTestState_t moduleExports UYA_MODULE_EXPORTS = {
	0x444e4153, sizeof(dnasTestState_t), 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const moduleApi_t* loader;

int moduleInit(const moduleApi_t* api)
{
	if (api->version != UYA_API_VERSION || api->region != 2) {
		return -1;
	}
	loader = api;
	moduleExports.initCount++;
	return 0;
}

void moduleUpdate(void)
{
	u32 result;

	moduleExports.mode = loader->mode;
	moduleExports.context = loader->context;
	if (loader->mode == UYA_MODE_CAMPAIGN) {
		moduleExports.campaignTicks++;
		moduleExports.level = *(volatile u32*)0x001d545c;
		return;
	}
	if (loader->mode != UYA_MODE_MULTIPLAYER) {
		return;
	}
	moduleExports.multiplayerTicks++;
	moduleExports.level = *(volatile u32*)0x001f83a8;
	if (moduleExports.level != 39) {
		return;
	}
	moduleExports.attempts++;
	result = dnasPayloadStart();
	moduleExports.lastResult = result;
	if (result == 1) {
		moduleExports.applied++;
		loader->print("racdoor-dnas-test: PAL DNAS patch applied\n");
	}
}
