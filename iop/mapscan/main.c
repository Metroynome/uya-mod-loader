/* Offline directory enumeration only. No USB drivers or network dependency. */
#include <loadcore.h>
#include <ioman.h>
#include <sifcmd.h>
#include <thbase.h>
#include <sysclib.h>

#define SERVICE (0x554d4151)

typedef struct mapScanReply {
	int status;
	char name[256];
	char pad[60];
} mapScanReply_t;

IRX_ID("uya_mapscan", 2, 0);
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static int request[16] __attribute__((aligned(64)));
static mapScanReply_t reply __attribute__((aligned(64)));
static int directory = -1;

static void* dispatch(int command, void* data, int size)
{
	io_dirent_t entry;
	int n;
	(void)data;
	(void)size;
	memset(&reply, 0, sizeof(reply));
	if (command == 1 || command == 3) {
		if (directory >= 0)
			dclose(directory);
		directory = -1;
		if (command == 1) {
			directory = dopen("host0:uya", 0);
			reply.status = directory < 0 ? directory : 1;
		}
	} else if (command == 2 && directory >= 0) {
		memset(&entry, 0, sizeof(entry));
		reply.status = dread(directory, &entry);
		if (reply.status > 0) {
			for (n = 0; n < sizeof(reply.name) - 1 && entry.name[n]; ++n)
				reply.name[n] = entry.name[n];
			/* Long filenames cannot fit our map ABI; ignore them safely. */
			if (entry.name[n])
				reply.name[0] = 0;
		}
	} else
		reply.status = -1;
	return &reply;
}

static void serve(void* arg)
{
	(void)arg;
	SifSetRpcQueue(&queue, GetThreadId());
	SifRegisterRpc(&server, SERVICE, dispatch, request, 0, 0, &queue);
	SifRpcLoop(&queue);
}

int _start(int argc, char** argv)
{
	iop_thread_t thread;
	int id;
	(void)argc;
	(void)argv;
	memset(&thread, 0, sizeof(thread));
	thread.attr = TH_C;
	thread.thread = serve;
	thread.priority = 40;
	thread.stacksize = 0x1000;
	SifInitRpc(0);
	id = CreateThread(&thread);
	if (id < 0)
		return MODULE_NO_RESIDENT_END;
	if (StartThread(id, 0) < 0) {
		DeleteThread(id);
		return MODULE_NO_RESIDENT_END;
	}
	return MODULE_RESIDENT_END;
}
