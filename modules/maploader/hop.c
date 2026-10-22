/* Local Play development transitions. The retail request only schedules the
 * change; the game's main loop performs teardown and loading after we return. */
#include "maploader.h"
#include "include/forge.h"
#define WORD(a) (*(volatile unsigned int *)(a))
static unsigned int targetBase;
static int queued=-1,phase,ticks;
int mapHopSawLoading;
int mapHopResult;
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
{return mapCatalogReady && mapForgeActive() && mapHopInGame() && !phase && !mapLoading() && !mapScanActive() && mapHooksReady() && findRequest()!=0;}
int mapHopRequest(int index)
{
    if(!mapHopAvailable() || (index<0?(index>-40 || index<-49):index>=mapCount))return 0;
    if(index>=0 && !mapValidate(&mapRecords[index])) {mapMessage("Files changed; refresh first");return 0;}
    targetBase=index<0?-index:mapRecords[index].base;
    mapHopResult=1;queued=index;phase=1;ticks=mapHopSawLoading=0;mapMessage("Map hop queued");return 1;
}
/* NTSC-U local staging update 0x6ab8e8 calls 0x6ac720 for its
 * Start Game action. Reuse retail roster/settings initialization unchanged.
 * Hash the whole callback before calling code from the staging overlay. */
#ifndef MAP_START_GAME_CALL
#define MAP_START_GAME_CALL(menu) ((void (*)(unsigned int))0x6ac720)(menu)
#endif
static unsigned int startGameMenu(void)
{
#if UYA_MODULE_REGION == 1
    unsigned int menu,hash=2166136261U,i;
    if(mapApi->context!=UYA_CONTEXT_LOCAL || mapStaging()!=1)return 0;
    menu=WORD(0x1c5c0c0);
    if(menu<0x100000 || menu>0x1fffc00 || (menu&3) ||
       WORD(menu+0x60)!=0x47de60)return 0;
    for(i=0;i<240;++i)hash=(hash^*(volatile unsigned char *)(0x6ac720+i))*16777619U;
    return hash==0xeca152d0U?menu:0;
#else
    return 0;
#endif
}
int mapStartGame(void)
{
    if(!mapForgeActive() || phase || mapLoading() || mapScanActive() ||
       !mapHooksReady() || !startGameMenu())return 0;
    if(mapSelection>=0 && (mapSelection>=mapCount || !mapValidate(&mapRecords[mapSelection])))return 0;
    mapHopResult=1;phase=3;ticks=0;mapMessage("Start Game queued");return 1;
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
    unsigned int settings,base,requestAddress;
    if(!phase)return;
    if(phase==3){
        unsigned int menu=startGameMenu();
        if(!menu || !mapForgeActive() || mapLoading()){mapHopCancel();return;}
        phase=4;mapMessage("Starting game...");MAP_START_GAME_CALL(menu);return;
    }
    if(phase==4 && WORD(0x1f8528)>=40 && WORD(0x1f8528)<=55 &&
       WORD(0x241bd0)==1 && WORD(0x2459c8)==1 && !mapLoading()){
        phase=0;mapHopResult=2;mapMessage("Game started");return;
    }
    if(phase==1) {
        if(!mapForgeActive() || !mapHopInGame() || mapLoading()) {mapHopCancel();return;}
        settings=WORD(0x2417c0);
        if(settings<0x100000 || settings>0x1fffe00 || (settings&3) || !findRequest()) {mapHopCancel();return;}
        requestAddress=findRequest();
        base=targetBase;
        mapSelection=queued<0?-1:queued;mapClearLoad();
        WORD(settings+0x17c)=base;
        phase=2;
        mapMessage("Loading map...");
        MAP_HOP_CALL(requestAddress,base);
        return;
    }
    ++ticks;
    /* mapHopOnLoad marks the one-time reset at the level-loading boundary. */
    if(phase==2 && mapHopSawLoading && mapHopInGame() && !mapLoading() &&
       WORD(0x1f8528)==targetBase) {
        int hudReady=restoreHud(WORD(0x1f8528));
        mapHopResult=hudReady?2:3;
        phase=0;queued=-1;
        mapMessage(hudReady?"Map hop complete":"HUD setup unavailable; return to staging");
    } else if(ticks>12000) {
        mapHopResult=3;phase=0;queued=-1;mapMessage("Load timeout; restart game");
    }
}
void mapHopCancel(void) {if(phase)mapHopResult=3;phase=0;queued=-1;}
