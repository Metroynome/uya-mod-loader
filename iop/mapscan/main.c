/* Host directory enumeration only. No USB drivers or network dependency.
 * Maps and music use independent handles on the same IOP RPC thread. */
#include <loadcore.h>
#include <ioman.h>
#include <sifcmd.h>
#include <thbase.h>
#include <sysclib.h>

#define SERVICE (0x554d4151)

typedef struct mapScanReply {
	/* 0x00 */ int status;
	/* 0x04 */ char name[256];
	/* 0x104 */ char pad[60];
} mapScanReply_t;

IRX_ID("uya_mapscan", 2, 0);
static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static SifRpcServerData_t musicServer;
static int request[16] __attribute__((aligned(64)));
static int musicRequest[16] __attribute__((aligned(64)));
static mapScanReply_t reply __attribute__((aligned(64)));
static int directory[2] = {-1, -1};

/*
 * NAME : readDirectory
 * DESCRIPTION : Enumerate map or music files with independent directory handles.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void* readDirectory(int command, int index)
{
	io_dirent_t entry;
	int n;
	memset(&reply, 0, sizeof(reply));
	if (command == 1 || command == 3) {
		if (directory[index] >= 0)
			dclose(directory[index]);
		directory[index] = -1;
		if (command == 1) {
			directory[index] = dopen(index ? "host0:uya/music" : "host0:uya", 0);
			reply.status = directory[index] < 0 ? directory[index] : 1;
		}
	} else if (command == 2 && directory[index] >= 0) {
		memset(&entry, 0, sizeof(entry));
		reply.status = dread(directory[index], &entry);
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

/*
 * NAME : dispatch
 * DESCRIPTION : Serve the existing map-directory RPC.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void* dispatch(int command, void* data, int size)
{
	(void)data;
	(void)size;
	return readDirectory(command, 0);
}

/*
 * NAME : dispatchMusic
 * DESCRIPTION : Serve music discovery without changing the map directory.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void* dispatchMusic(int command, void* data, int size)
{
	(void)data;
	(void)size;
	return readDirectory(command, 1);
}

/*
 * NAME : serve
 * DESCRIPTION : Register both directory services on the shared RPC queue.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void serve(void* arg)
{
	(void)arg;
	SifSetRpcQueue(&queue, GetThreadId());
	SifRegisterRpc(&server, SERVICE, dispatch, request, 0, 0, &queue);
	SifRegisterRpc(&musicServer, 0x554d5553, dispatchMusic, musicRequest, 0, 0, &queue);
	SifRpcLoop(&queue);
}

/*
 * NAME : _start
 * DESCRIPTION : Start the shared directory-helper thread.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
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
