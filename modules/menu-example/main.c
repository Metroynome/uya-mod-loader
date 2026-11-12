#include "modules/modmenu/include/modmenu.h"

static const moduleApi_t* loader;
static int registered, enabled, amount = 5;
static const moduleConfigField_t settings[] = {{"enabled", &enabled, 0, 1}, {"amount", &amount, 0, 10}};
static const modMenuApi_t* registeredMenu;

static void deactivate(void)
{
	if (registeredMenu && registered)
		registeredMenu->removeTab(registered);
	registered = 0;
	registeredMenu = 0;
}

static void action(void* context)
{
	(void)context;
	loader->print("menu-example: action, enabled=%d amount=%d mode=%u\n", enabled, amount, loader->mode);
}

static void configAction(void* saving)
{
	int result =
	    saving ? loader->saveConfig("menu-example", settings, 2) : loader->loadConfig("menu-example", settings, 2);
	loader->print("menu-example: %s settings result=%d\n", saving ? "save" : "load", result);
}

int moduleInit(const moduleApi_t* api)
{
	loader = api;
	if (api->version < UYA_API_VERSION || api->size < sizeof(*api))
		return -1;
	api->loadConfig("menu-example", settings, 2);
	api->setLifecycle(0, deactivate);
	return 0;
}

void moduleUpdate(void)
{
	const modMenuApi_t* menu;
	int tab;
	modMenuItem_t toggle = {"Example toggle", MODMENU_TOGGLE, &enabled, 0, 0, 0, action, 0};
	modMenuItem_t range = {"Example value", MODMENU_RANGE, &amount, 0, 10, 1, action, 0};
	static const modMenuItem_t config = {"", MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0};
	static const modMenuButton_t configButtons[] = {
		{"Load Settings", configAction, 0},
		{"Save Settings", configAction, (void*)1}
	};
	int configItem;
	if (registered)
		return;
	menu = modmenuFind(loader);
	if (!menu)
		return; /* Load-order independent. */
	tab = menu->addTab("menu-example", "Example Mod", UYA_MODE_BOTH);
	if (tab < 0)
		return;
	if (menu->addItem(tab, &toggle) < 0 || menu->addItem(tab, &range) < 0 ||
	    (configItem = menu->addItem(tab, &config)) < 0 || menu->setItemButtons(configItem, configButtons, 2) < 0) {
		menu->removeTab(tab);
		return;
	}
	menu->setTabContexts(tab, UYA_CONTEXT_CAMPAIGN | UYA_CONTEXT_LOCAL);
	registered = tab;
	registeredMenu = menu;
}
