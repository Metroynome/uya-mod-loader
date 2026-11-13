#include "requirements.h"
#include "config.h"
#include "strings.h"
#include "modules/modmenu/include/modmenu.h"
#include "modules/modmenu/backend.h"

#define REQUIREMENT_NAME_BYTES (48)

typedef struct requirementNotice {
	/* 0x00 */ char name[REQUIREMENT_NAME_BYTES];
	/* 0x30 */ u32 required;
	/* 0x34 */ u32 shown;
} requirementNotice_t;

static int drawRequirements(u32 pressed);

static requirementNotice_t notices[UYA_MODULE_LIMIT];
static const modMenuApi_t* menu;
static int noticeCount, currentNotice, extraRamAvailable;
static int ownsModal, ownsFallback;
static u32 previousButtons, closingButtons, previousMode;

/*
 * NAME : moduleRequirementsReset
 * DESCRIPTION : Initialize requirement notices for this boot.
 * NOTES : Expanded RAM means the loader actually selected its safe extra-RAM pool.
 * ARGS : Nonzero if the expanded module pool is available.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleRequirementsReset(int extraRam)
{
	memset(notices, 0, sizeof(notices));
	noticeCount = 0;
	currentNotice = -1;
	extraRamAvailable = extraRam;
	menu = 0;
	ownsModal = ownsFallback = 0;
	previousButtons = closingButtons = previousMode = 0;
}

/*
 * NAME : moduleRequirementsMissing
 * DESCRIPTION : Return only requirements unavailable in the current context.
 * NOTES : Mod Menu must publish its service in the module's current mode.
 * ARGS : Required feature bits.
 * RETURN : Missing feature bits.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 moduleRequirementsMissing(u32 required)
{
	const moduleApi_t* api = moduleConfigApi();
	u32 missing = 0;
	if ((required & UYA_REQUIRE_EXTRA_RAM) && !extraRamAvailable)
		missing |= UYA_REQUIRE_EXTRA_RAM;
	if ((required & UYA_REQUIRE_MODMENU) && !modmenuFind(api))
		missing |= UYA_REQUIRE_MODMENU;
	return missing;
}

/*
 * NAME : moduleRequirementsNotice
 * DESCRIPTION : Queue one warning per module for this game boot.
 * NOTES : Only the base module name is displayed.
 * ARGS : Module filename and required feature bits.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleRequirementsNotice(const char* name, u32 required)
{
	char label[REQUIREMENT_NAME_BYTES];
	int i, n = 0;
	while (name[n] && name[n] != '.' && n < REQUIREMENT_NAME_BYTES - 1) {
		label[n] = name[n];
		++n;
	}
	label[n] = 0;
	for (i = 0; i < noticeCount; ++i) {
		for (n = 0; label[n] && label[n] == notices[i].name[n]; ++n) {
		}
		if (!label[n] && !notices[i].name[n])
			return;
	}
	if (noticeCount == UYA_MODULE_LIMIT)
		return;
	strcpy(notices[noticeCount].name, label);
	notices[noticeCount++].required = required;
}

/*
 * NAME : drawRequirements
 * DESCRIPTION : Draw a BIOS-warning-style list of missing module requirements.
 * NOTES : Mod Menu supplies input capture when available.
 * ARGS : Newly pressed controller buttons.
 * RETURN : Nonzero while the warning remains open.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int drawRequirements(u32 pressed)
{
	void (*box)(float, float, float, float, u32) = menu ? menu->drawBox : menuBackendBox;
	void (*text)(float, float, u32, const char*) = menu ? menu->drawText : menuBackendText;
	requirementNotice_t* notice = &notices[currentNotice];
	u32 missing = moduleRequirementsMissing(notice->required);
	float y = .50f;
	if (!missing || (pressed & 0x4000)) {
		notice->shown = 1;
		ownsModal = 0;
		return 0;
	}
	box(0, 0, 1, 1, 0x50000000);
	box(.10f, .27f, .80f, .46f, 0x8018608f);
	box(.11f, .28f, .78f, .44f, 0x80123251);
	text(.15f, .32f, 0x8069cbf2, "MISSING MODULE REQUIREMENTS");
	text(.15f, .40f, 0x8069cbf2, notice->name);
	text(.15f, .45f, 0x8069cbf2, "Enable these items, then restart:");
	if (missing & UYA_REQUIRE_MODMENU) {
		text(.15f, y, 0x8069cbf2, "- Mod Menu (in this mode)");
		y += .05f;
	}
	if (missing & UYA_REQUIRE_EXTRA_RAM)
		text(.15f, y, 0x8069cbf2, "- 128 MiB RAM + compatible BIOS");
	text(.15f, .66f, 0x8069cbf2, "\x10 Okay");
	return 1;
}

/*
 * NAME : moduleRequirementsTick
 * DESCRIPTION : Display pending warnings without requiring Mod Menu itself.
 * NOTES : Reuses the verified backend; wait until boot movies finish.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleRequirementsTick(void)
{
	const moduleApi_t* api = moduleConfigApi();
	u32 buttons, missing;
	int i;
	if (ownsModal) {
		if (menu == modmenuFind(api))
			return;
		menu->setModal(0);
		menu = 0;
		ownsModal = 0;
	}
	if (previousMode && previousMode != api->mode) {
		menuBackendRelease();
		ownsFallback = 0;
		closingButtons = previousButtons = 0;
	}
	previousMode = api->mode;
	if (!ownsFallback && !closingButtons) {
		for (i = 0; i < noticeCount; ++i) {
			missing = moduleRequirementsMissing(notices[i].required);
			if (missing && !notices[i].shown)
				break;
		}
		if (i == noticeCount)
			return;
		currentNotice = i;
		menu = modmenuFind(api);
		if (menu) {
			if (menu->setModal(drawRequirements))
				ownsModal = 1;
			return;
		}
	}
	if (!menuBackendReady(api->mode) ||
	    (!ownsFallback && !closingButtons && !menuBackendPopupReady(api->mode))) {
		menuBackendRelease();
		ownsFallback = 0;
		previousButtons = closingButtons = 0;
		return;
	}
	buttons = menuBackendButtons(api->mode);
	if (closingButtons) {
		if (buttons & closingButtons)
			menuBackendCapture(api->mode);
		else {
			closingButtons = 0;
			menuBackendRelease();
		}
		return;
	}
	if (!ownsFallback) {
		previousButtons = buttons;
		ownsFallback = 1;
		menu = 0;
	}
	menuBackendCapture(api->mode);
	if (!drawRequirements(buttons & ~previousButtons)) {
		closingButtons = buttons;
		ownsFallback = 0;
		if (!buttons)
			menuBackendRelease();
	}
	previousButtons = buttons;
}
