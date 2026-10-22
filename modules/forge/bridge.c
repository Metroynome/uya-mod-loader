/* PINE only writes data; commands execute here on the normal module tick. */
#include "include/forge.h"

/* Part of Forge's BSS and module memory size, not a loader reservation.
 * PINE discovers the aligned FRG1 header; see PINE.md. */
volatile ForgeMailbox forgeMailbox __attribute__((aligned(16)));
static unsigned int seen, running;

static void message(const char* s)
{
	unsigned int i;
	for (i = 0; i < 47 && s && s[i]; ++i)
		FORGE_MAILBOX->message[i] = s[i];
	FORGE_MAILBOX->message[i] = 0;
}

void forgeBridgeDisable(void)
{
	volatile ForgeMailbox* b = FORGE_MAILBOX;
	b->available = b->capabilities = b->context = 0;
	if (running) {
		b->state = FORGE_FAILED;
		b->error = FORGE_ERROR_INACTIVE;
		b->completedId = running;
		running = 0;
	}
	seen = b->requestId;
	message("Forge inactive");
}

void forgeBridgeInit(void)
{
	volatile ForgeMailbox* b = FORGE_MAILBOX;
	unsigned int i;
	for (i = 0; i < sizeof(*b) / 4; ++i)
		((volatile unsigned int*)b)[i] = 0;
	seen = running = 0;
	b->version = 1;
	b->size = sizeof(*b);
	b->magic = FORGE_MAILBOX_MAGIC;
}

void forgeBridgeTick(const MapHopApi* p)
{
	volatile ForgeMailbox* b = FORGE_MAILBOX;
	unsigned int id, command, i;
	char name[64];
	int error;
	++b->heartbeat;
	b->available = p != 0;
	b->context = p ? p->context() : 0;
	b->capabilities = b->context == 1 ? 124 : b->context == 2 ? 94 : 0;
	if (running) {
		b->state = p ? p->state() : FORGE_FAILED;
		b->error = p ? p->error() : FORGE_ERROR_INACTIVE;
		message(p ? p->status() : "Map Loader unavailable");
		if (b->state == FORGE_COMPLETE || b->state == FORGE_FAILED) {
			b->completedId = running;
			running = 0;
		}
	}
	id = b->requestId;
	if (!id || id == seen)
		return;
	command = b->command;
	for (i = 0; i < 64; ++i)
		name[i] = b->filename[i];
	if (id != b->requestId)
		return; /* Host is still submitting a newer request. */
	seen = id;
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
	message(p->status());
	if (b->state == FORGE_COMPLETE || b->state == FORGE_FAILED) {
		b->completedId = id;
		running = 0;
	}
}
