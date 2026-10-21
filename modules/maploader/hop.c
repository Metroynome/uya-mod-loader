/* Local Play development transitions. The retail request only schedules the
 * change; the game's main loop performs teardown and loading after we return. */
#include "maploader.h"
#include "include/forge.h"
#define WORD(a) (*(volatile unsigned int *)(a))
static unsigned int requestAddress;
static int queued=-1,phase,ticks;
int mapHopSawLoading;
#ifndef MAP_HOP_CALL
#define MAP_HOP_CALL(address,base) ((void (*)(int))(address))(base)
#endif
#ifndef MAP_HOP_HUD_CALL
#define MAP_HOP_HUD_CALL(address) ((void (*)(void))(address))()
#endif
/* The retail HUD loader only registers gameplay layers when the previous level
 * was staging (39). Reloads can clear the registrations even on the same base.
 * Run retail setup once after every completed Forge transition; its individual
 * registration routines guard their own initialization. */
static int restoreHud(unsigned int level)
{
#if UYA_MODULE_REGION == 1
    static const unsigned int addresses[]={0x4a6a30,0x4a8a88,0x49e3a0,0x49bbb8,
        0x49af08,0x498720,0x4988d8,0x49b118,0x49a458,0x499d98};
    static const unsigned int signature[]={0x27bdffe0,0x3c030024,0x8c6312a0,
        0xffb00000,0x0000202d,0xffb10008,0x24100004,0xffbf0010,0x0000882d,
        0x8c620000,0x28420002};
    unsigned int a,i;
    if(level<40 || level>49)return 0;
    a=addresses[level-40];
    for(i=0;i<sizeof(signature)/sizeof(signature[0]);++i)
        if(WORD(a+i*4)!=signature[i])return 0;
    MAP_HOP_HUD_CALL(a);
    return 1;
#else
    return 0;
#endif
}
int mapForgeActive(void)
{
    const ForgeApi *p=mapApi->findService(FORGE_SERVICE_ID,FORGE_API_VERSION);
    return p && p->version==FORGE_API_VERSION && p->size>=sizeof(*p) && p->active();
}
int mapHopInGame(void)
{
#if UYA_MODULE_REGION == 1
    unsigned int level=WORD(0x1f8528);
    return mapApi->context==UYA_CONTEXT_LOCAL && mapMultiplayerReady() &&
        level>=40 && level<=49 && WORD(0x241bd0)==1 && WORD(0x2459c8)==1;
#else
    return 0; /* PAL transition qualification is still required. */
#endif
}
static unsigned int findRequest(void)
{
    unsigned int a=mapSceneRequest;
    return a && WORD(a)==0x27bdffe0 && WORD(a+4)==0xafa40000 &&
        WORD(a+8)==0xffbf0010 && WORD(a+40)==0xaf82a934?a:0;
}
int mapHopAvailable(void)
{return mapForgeActive() && mapHopInGame() && !phase && !mapLoading() && !mapScanActive() && mapHooksReady() && findRequest()!=0;}
int mapHopRequest(int index)
{
    if(!mapHopAvailable() || index<0 || index>=mapCount)return 0;
    if(!mapValidate(&mapRecords[index])) {mapMessage("Map files changed; refresh before reloading");return 0;}
    queued=index;phase=1;ticks=mapHopSawLoading=0;mapMessage("Map hop queued");return 1;
}
/* The ordinary transition preserves local network-player records. Re-entering
 * local player creation with pNetPlayerData still set makes 0x19a2c0 return
 * without attaching a player, and its caller dereferences NULL. Reset the same
 * records retail cleanup clears, after teardown reaches the new level TOC.
 * The game recreates these records from the existing match settings. */
void mapHopOnLoad(void)
{
#if UYA_MODULE_REGION == 1
    unsigned int i,j;
    if(phase!=2 || mapHopSawLoading || mapApi->context!=UYA_CONTEXT_LOCAL)return;
    for(i=0;i<8;++i) {
        volatile unsigned int *p=(volatile unsigned int *)(0x22f550+i*0xc70);
        for(j=0;j<0xc70/4;++j)p[j]=0;
        p[0]=0xffffffff;
    }
    WORD(0x2417c4)=0;
    mapHopSawLoading=1;
#endif
}
void mapHopTick(void)
{
    unsigned int settings,base;
    if(!phase)return;
    if(phase==1) {
        if(!mapForgeActive() || !mapHopInGame() || mapLoading()) {mapHopCancel();return;}
        settings=WORD(0x2417c0);
        if(settings<0x100000 || settings>0x1fffe00 || (settings&3) || !findRequest()) {mapHopCancel();return;}
        requestAddress=findRequest();
        base=mapRecords[queued].base;
        mapSelection=queued;mapClearLoad();
        WORD(settings+0x17c)=base;
        phase=2;
        mapMessage("Loading custom map...");
        MAP_HOP_CALL(requestAddress,base);
        return;
    }
    ++ticks;
    /* mapHopOnLoad marks the one-time reset at the level-loading boundary. */
    if(mapHopSawLoading && mapHopInGame() && !mapLoading() &&
       WORD(0x1f8528)==mapRecords[queued].base) {
        int hudReady=restoreHud(WORD(0x1f8528));
        phase=0;queued=-1;requestAddress=0;
        mapMessage(hudReady?"Map hop complete":"HUD setup unavailable; return to staging");
    } else if(ticks>12000) {
        phase=0;queued=-1;mapMessage("Map transition timed out; restart if needed");
    }
}
void mapHopCancel(void) {phase=0;queued=-1;requestAddress=0;}
