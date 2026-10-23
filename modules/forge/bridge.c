/* PINE only writes data; commands execute here on the normal module tick. */
#include "modules/forge/include/forge.h"

/* Part of Forge's BSS and module memory size, not a loader reservation.
 * PINE follows the module discovery header and exports table; see PINE.md. */
volatile forgeMailbox_t forgeMailbox __attribute__((aligned(16)));
const forgeExports_t moduleExports UYA_MODULE_EXPORTS = {FORGE_EXPORTS_MAGIC, 1, sizeof(forgeExports_t), &forgeMailbox};
static u32 seen, running;

void forgeBridgeDisable(void)
{
	volatile forgeMailbox_t* b = FORGE_MAILBOX;
	b->available = b->capabilities = b->context = 0;
	if (running) {
		b->state = FORGE_FAILED;
		b->error = FORGE_ERROR_INACTIVE;
		b->completedId = running;
		running = 0;
	}
	seen = b->requestId;
}

void forgeBridgeInit(void)
{
	volatile forgeMailbox_t* b = FORGE_MAILBOX;
	u32 i;
	for (i = 0; i < sizeof(*b) / 4; ++i)
		((volatile u32*)b)[i] = 0;
	seen = running = 0;
	b->version = FORGE_MAILBOX_VERSION;
	b->size = sizeof(*b);
	b->magic = FORGE_MAILBOX_MAGIC;
}

void forgeBridgeTick(const mapHopApi_t* p)
{
	volatile forgeMailbox_t* b = FORGE_MAILBOX;
	u32 id, command, i;
	char name[FORGE_FILENAME_SIZE];
	int error;
	++b->heartbeat;
	b->available = p != 0;
	b->context = p ? p->context() : 0;
	b->capabilities = b->context == 1 ? 124 : b->context == 2 ? 94 : 0;
	if (running) {
		b->state = p ? p->state() : FORGE_FAILED;
		b->error = p ? p->error() : FORGE_ERROR_INACTIVE;
		if (b->state == FORGE_COMPLETE || b->state == FORGE_FAILED) {
			b->completedId = running;
			running = 0;
		}
	}
	id = b->requestId;
	if (!id || id == seen)
		return;
	command = b->command;
	for (i = 0; i < sizeof(name); ++i)
		name[i] = b->filename[i];
	if (id != b->requestId)
		return; /* Host is still submitting a newer request. */
	seen = id;
	for (i = 0; i < sizeof(name) && name[i]; ++i)
		;
	if ((command == FORGE_SELECT || command == FORGE_REFRESH_SELECT || command == FORGE_SELECT_RETAIL) &&
	    i == sizeof(name)) {
		b->rejectError = FORGE_ERROR_NAME;
		b->rejectedId = id;
		return;
	}
	error = running ? FORGE_ERROR_BUSY : !p ? FORGE_ERROR_INACTIVE : p->request(command, name);
	if (error) {
		b->rejectError = error;
		b->rejectedId = id;
		return;
	}
	b->acceptedId = id;
	running = id;
	b->state = p->state();
	b->error = 0;
	if (b->state == FORGE_COMPLETE || b->state == FORGE_FAILED) {
		b->completedId = id;
		running = 0;
	}
}
