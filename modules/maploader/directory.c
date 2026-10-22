/* Resident NTSC-U/PAL multiplayer SDK entry points, shared by both regions. */
#include "maploader.h"
#include <sifrpc.h>
#include <sifcmd.h>
#include <kernel.h>

#define CALL(a, t) ((t)(a))
#define bindRpc    CALL(0x129c38, int (*)(SifRpcClientData_t*, int, int))
#define callRpc    CALL(0x129e18, int (*)(SifRpcClientData_t*, int, int, void*, int, void*, int, void*, void*))
#define rpcBusy    CALL(0x12a018, int (*)(SifRpcClientData_t*))

struct MapScanReply {
	int status;
	char name[128];
	char pad[60];
};

static SifRpcClientData_t client __attribute__((aligned(64)));
static int request[16] __attribute__((aligned(64)));
static struct MapScanReply reply __attribute__((aligned(64)));
static int pending, loading, attempted, bindRetries;

void* mapAlloc(unsigned int size)
{
	return CALL(0x11c7b8, void* (*)(unsigned int))(size);
}

void mapFree(void* p)
{
	if (p && mapApi->mode == UYA_MODE_MULTIPLAYER)
		CALL(0x11c810, void (*)(void*))(p);
}

static int install(void)
{
	int fd, size, id = -1, result = -1, q, n;
	void *raw = 0, *aligned, *remote = 0;
	SifDmaTransfer_t dma;
	fd = mapApi->open("host0:uya/mods/utils/mapscan.irx", 1, 0);
	if (fd < 0)
		return -1;
	size = CALL(0x12ae40, int (*)(int, int, int))(fd, 0, 2);
	if (size <= 0 || size > 0x10000)
		goto done;
	CALL(0x12ae40, int (*)(int, int, int))(fd, 0, 0);
	raw = mapAlloc(size + 79);
	if (!raw)
		goto done;
	aligned = (void*)(((unsigned int)raw + 63) & ~63U);
	memset(aligned, 0, (size + 15) & ~15);
	if (!mapReadExact(fd, aligned, size))
		goto done;
	CALL(0x12b8b8, int (*)(void))();
	remote = CALL(0x12b940, void* (*)(int))((size + 15) & ~15);
	if (!remote)
		goto done;
	dma.src = aligned;
	dma.dest = remote;
	dma.size = (size + 15) & ~15;
	dma.attr = 0;
	CALL(0x1293e0, void (*)(void*, int))(aligned, dma.size);
	q = CALL(0x126620, int (*)(SifDmaTransfer_t*, int))(&dma, 1);
	if (!q)
		goto done;
	/* The DMA must finish before either source or destination can be freed. */
	for (n = 0; CALL(0x126600, int (*)(int))(q) >= 0; ++n) {
		if (n == 1000000) {
			raw = remote = 0;
			goto done;
		} /* Quarantine in-flight DMA buffers on failure. */
	}
	/* The 0x12c2c0 wrapper discards the result pointer. This variant
     * forwards it to the shared implementation at 0x12bd80. */
	id = CALL(0x12c2e0, int (*)(void*, int, const char*, int*))(remote, 0, 0, &result);
	if (result < 0)
		id = -1;
done:
	if (remote)
		CALL(0x12baa8, int (*)(void*))(remote);
	mapFree(raw);
	mapApi->close(fd);
	mapApi->print("maploader: directory helper id=%d result=%d\n", id, result);
	return id;
}

int mapDirectoryIdle(void)
{
	return !rpcBusy(&client);
}

void mapDirectoryReset(void)
{
	pending = loading = attempted = bindRetries = 0;
	memset(&client, 0, sizeof(client));
}

/* 0 pending, 1 ready, -1 failure. No unbounded server-binding loop. */
int mapDirectoryReady(void)
{
	if (pending) {
		if (rpcBusy(&client))
			return 0;
		pending = 0;
		if (client.server)
			return 1;
		/* The new IOP thread may register its RPC server a few frames later. */
		if (attempted)
			return ++bindRetries >= 120 ? -1 : 0;
		loading = 1;
		return 0;
	}
	if (loading) {
		loading = 0;
		attempted = 1;
		if (install() < 0)
			return -1;
	}
	memset(&client, 0, sizeof(client));
	if (bindRpc(&client, 0x554d4150, SIF_RPC_M_NOWAIT) < 0)
		return -1;
	pending = 1;
	return 0;
}

int mapDirectoryRequest(int command)
{
	if (callRpc(&client, command, SIF_RPC_M_NOWAIT, request, sizeof(request), &reply, sizeof(reply), 0, 0) < 0)
		return 0;
	return 1;
}

int mapDirectoryPoll(char* name)
{
	int i;
	if (rpcBusy(&client))
		return -999;
	if (name) {
		for (i = 0; i < 128; ++i)
			name[i] = reply.name[i];
		name[127] = 0;
	}
	return reply.status;
}
