/* PINE only writes data; commands execute here on the normal module tick. */
#include "modules/forge/include/forge.h"

int forgeBootAvailable(void);
int forgeBootRequest(u32 requestId);
u32 forgeBootState(void);

/* Part of Forge's BSS and module memory size, not a loader reservation.
 * PINE follows the module discovery header and exports table; see PINE.md. */
volatile forgeMailbox_t forgeMailbox __attribute__((aligned(16)));
const forgeExports_t moduleExports UYA_MODULE_EXPORTS = {FORGE_EXPORTS_MAGIC, 1, sizeof(forgeExports_t), &forgeMailbox};
static u32 lastRequestId, activeRequestId;
static int bootRequest;
static const u8 stagingCommands = (1 << FORGE_REFRESH) | (1 << FORGE_SELECT) |
	                             (1 << FORGE_REFRESH_SELECT) | (1 << FORGE_SELECT_RETAIL) |
	                             (1 << FORGE_START_GAME);
static const u8 gameCommands = (1 << FORGE_RELOAD) | (1 << FORGE_REFRESH) | (1 << FORGE_SELECT) |
                              (1 << FORGE_REFRESH_SELECT) | (1 << FORGE_SELECT_RETAIL);

void forgeBridgeDisable(void)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	mailbox->available = mailbox->capabilities = mailbox->context = 0;
	/* Entering multiplayer briefly crosses an unavailable front-end context.
	 * Keep this one operation pending until native staging is actually ready. */
	if (activeRequestId && !(bootRequest && forgeBootState() == FORGE_LOADING)) {
		mailbox->state = FORGE_FAILED;
		mailbox->error = FORGE_ERROR_INACTIVE;
		mailbox->completedId = activeRequestId;
		activeRequestId = 0;
		bootRequest = 0;
	}
	lastRequestId = mailbox->requestId;
}

void forgeBridgeInit(void)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	u32 i;
	for (i = 0; i < sizeof(*mailbox) / 4; ++i)
		((volatile u32*)mailbox)[i] = 0;
	lastRequestId = activeRequestId = 0;
	bootRequest = 0;
	mailbox->version = FORGE_MAILBOX_VERSION;
	mailbox->size = sizeof(*mailbox);
	mailbox->magic = FORGE_MAILBOX_MAGIC;
}

void forgeBridgeResume(u32 requestId)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	lastRequestId = activeRequestId = requestId;
	bootRequest = 1;
	mailbox->requestId = mailbox->acceptedId = requestId;
	mailbox->command = FORGE_LOCAL_PLAY;
	mailbox->state = FORGE_LOADING;
}

void forgeBridgeTick(const mapHopApi_t* hopApi)
{
	volatile forgeMailbox_t* mailbox = FORGE_MAILBOX;
	u32 id, command, i;
	char name[FORGE_FILENAME_SIZE];
	int error;
	int bootAvailable = forgeBootAvailable();
	++mailbox->heartbeat;
	mailbox->available = bootAvailable || hopApi != 0;
	mailbox->context = bootAvailable ? FORGE_CONTEXT_BOOT :
	                   hopApi ? hopApi->context() : FORGE_CONTEXT_UNAVAILABLE;
	mailbox->capabilities = mailbox->context == FORGE_CONTEXT_STAGING ? stagingCommands :
	                  mailbox->context == FORGE_CONTEXT_GAME ? gameCommands :
	                  mailbox->context == FORGE_CONTEXT_BOOT ? (1 << FORGE_LOCAL_PLAY) : 0;
	if (activeRequestId) {
		if (bootRequest) {
			mailbox->state = forgeBootState();
			mailbox->error = mailbox->state == FORGE_FAILED ? FORGE_ERROR_LOAD : 0;
		} else {
			mailbox->state = hopApi ? hopApi->state() : FORGE_FAILED;
			mailbox->error = hopApi ? hopApi->error() : FORGE_ERROR_INACTIVE;
		}
		if (mailbox->state == FORGE_COMPLETE || mailbox->state == FORGE_FAILED) {
			mailbox->completedId = activeRequestId;
			activeRequestId = 0;
			bootRequest = 0;
		}
	}
	id = mailbox->requestId;
	if (!id || id == lastRequestId)
		return;
	command = mailbox->command;
	for (i = 0; i < sizeof(name); ++i)
		name[i] = mailbox->filename[i];
	if (id != mailbox->requestId)
		return; /* Host is still submitting a newer request. */
	lastRequestId = id;
	for (i = 0; i < sizeof(name) && name[i]; ++i)
		;
	if ((command == FORGE_SELECT || command == FORGE_REFRESH_SELECT || command == FORGE_SELECT_RETAIL) &&
	    i == sizeof(name)) {
		mailbox->rejectError = FORGE_ERROR_NAME;
		mailbox->rejectedId = id;
		return;
	}
	if (activeRequestId)
		error = FORGE_ERROR_BUSY;
	else if (command == FORGE_LOCAL_PLAY)
		error = forgeBootRequest(id);
	else if (!hopApi)
		error = FORGE_ERROR_INACTIVE;
	else
		error = hopApi->request(command, name);
	if (error) {
		mailbox->rejectError = error;
		mailbox->rejectedId = id;
		return;
	}
	mailbox->acceptedId = id;
	activeRequestId = id;
	bootRequest = command == FORGE_LOCAL_PLAY;
	mailbox->state = bootRequest ? forgeBootState() : hopApi->state();
	mailbox->error = 0;
	if (mailbox->state == FORGE_COMPLETE || mailbox->state == FORGE_FAILED) {
		mailbox->completedId = id;
		activeRequestId = 0;
		bootRequest = 0;
	}
}
