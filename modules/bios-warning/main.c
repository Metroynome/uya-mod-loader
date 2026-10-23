#include "include/modmenu.h"

static const ModuleApi_t* loader;
static const ModMenuApi_t* menu;
static int shown, ownsPopup;

static int drawWarning(u32 pressed)
{
	menu->drawBox(0, 0, 1, 1, 0x50000000);
	menu->drawBox(.10f, .27f, .80f, .46f, 0x8018608f);
	menu->drawBox(.11f, .28f, .78f, .44f, 0x80123251);
	menu->drawText(.15f, .32f, 0x8069cbf2, "EXPANDED RAM: INCOMPATIBLE BIOS");
	menu->drawText(.15f, .40f, 0x8069cbf2, "Disable 128 MiB RAM in PCSX2, or select");
	menu->drawText(.15f, .45f, 0x8069cbf2, "a newer BIOS. Then restart emulation.");
	menu->drawText(.15f, .53f, 0x8069cbf2, "Entering multiplayer can still crash.");
	menu->drawText(.15f, .58f, 0x8069cbf2, "Modules are using normal RAM for now.");
	menu->drawText(.15f, .66f, 0x8069cbf2, "\x10 Dismiss warning");
	if (pressed & (0x1000 | 0x4000)) {
		ownsPopup = 0;
		return 0;
	}
	return 1;
}

static void deactivate(void)
{
	if (menu && ownsPopup) {
		menu->setModal(0);
		ownsPopup = 0;
		shown = 0;
	}
	menu = 0;
}

int moduleInit(const ModuleApi_t* api)
{
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	loader = api;
	api->setLifecycle(0, deactivate);
	return 0;
}

void moduleUpdate(void)
{
	if (shown || !(loader->warnings & UYA_WARNING_EXTRA_RAM_BIOS))
		return;
	menu = modmenuFind(loader);
	if (!menu)
		return;
	/* Load-order independent; defer while another popup is active. */
	if (menu->setModal(drawWarning)) {
		shown = 1;
		ownsPopup = 1;
	}
}
