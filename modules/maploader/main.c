/* Standalone Custom Maps tab. Map selection is independent of menu internals;
 * file IO and game hooks live in files.c and multiplayer.c respectively. */
#include "include/modmenu.h"
#include "maploader.h"
#include "include/forge.h"
static const ModMenuApi *menu;
static int tab,dirty=1,lastContext=-1;
static char status[48]="Choose a map in multiplayer staging";

static void copy(char *dest,const char *src,unsigned int cap)
{ unsigned int i=0; while(src[i] && i+1<cap) { dest[i]=src[i]; ++i; } dest[i]=0; }
void mapMessage(const char *message) { copy(status,message,sizeof(status)); dirty=1; }
static int reloadMap(void) {return mapHopRequest(mapSelection<0?-(int)mapCurrentBase():mapSelection);}
static const char *hopStatus(void) {return status;}
static unsigned int commandContext(void),commandState(void),commandError(void);
static int commandRequest(unsigned int,const char *);
static const MapHopApi hopService={FORGE_API_VERSION,sizeof(MapHopApi),mapHopAvailable,reloadMap,hopStatus,commandContext,commandRequest,commandState,commandError};
static int choose(void *context)
{
    int index=(int)context-1;
    if(mapScanActive())return 0;
    if(mapHopInGame())return mapHopRequest(index);
    if(!mapStaging() || !mapHooksReady() || mapLoading() || index<0 || index>=mapCount) return 0;
    if(!mapValidate(&mapRecords[index])) { mapMessage("Map files changed: re-enter staging"); return 0; }
    mapSelection=index;
    mapMessage(mapChooseBase(mapRecords[index].base)?"Selected; local base map updated":
               "Selected; host must choose its retail base");
    return 1;
}
static int chooseRetail(unsigned int base)
{
    if(mapScanActive() || mapLoading() || base<40 || base>49)return 0;
    if(mapHopInGame())return mapHopRequest(-(int)base);
    return mapChooseRetail(base);
}
/* Shared asynchronous requests; usable without a menu service. */
static unsigned int jobState,jobError,jobCommand;
static char jobName[64];
static unsigned int commandState(void){return jobState;}
static unsigned int commandError(void){return jobError;}
static unsigned int commandContext(void)
{
    if(mapApi->context!=UYA_CONTEXT_LOCAL || !mapForgeActive() || !mapHooksReady())return 0;
    return mapStaging()==1?1:mapHopInGame()?2:0;
}
static void commandFinish(unsigned int error)
{jobError=error;jobState=error?FORGE_FAILED:FORGE_COMPLETE;}
static int commandSelect(void)
{
    int i,j;
    if(jobCommand==FORGE_SELECT_RETAIL){
        if(jobName[0]!='4' || jobName[1]<'0' || jobName[1]>'9' || jobName[2])return FORGE_ERROR_NAME;
        if(!chooseRetail(40+jobName[1]-'0'))return FORGE_ERROR_LOAD;
        jobState=mapHopInGame()?FORGE_LOADING:FORGE_COMPLETE;dirty=1;return 0;
    }
    for(i=0;i<mapCount;++i){
        for(j=0;jobName[j] && jobName[j]==mapRecords[i].filename[j];++j);
        if(!jobName[j] && !mapRecords[i].filename[j]){
            if(!choose((void *)(i+1)))return FORGE_ERROR_LOAD;
            jobState=mapHopInGame()?FORGE_LOADING:FORGE_COMPLETE;dirty=1;return 0;
        }
    }
    return FORGE_ERROR_NOT_FOUND;
}
static int commandRequest(unsigned int command,const char *name)
{
    unsigned int context=commandContext(),i;
    if(jobState==FORGE_SCANNING || jobState==FORGE_LOADING || mapScanActive() || mapLoading() || mapHopResult==1)return FORGE_ERROR_BUSY;
    if(!context)return FORGE_ERROR_CONTEXT;
    if(command<FORGE_RELOAD || command>FORGE_SELECT_RETAIL)return FORGE_ERROR_COMMAND;
    if(command==FORGE_RELOAD && context!=2)return FORGE_ERROR_CONTEXT;
    if(command==FORGE_START_GAME && context!=1)return FORGE_ERROR_CONTEXT;
    if(command==FORGE_SELECT || command==FORGE_REFRESH_SELECT || command==FORGE_SELECT_RETAIL){
        if(!name || !name[0] || name[0]=='.')return FORGE_ERROR_NAME;
        for(i=0;i<64 && name[i];++i){unsigned char c=name[i];
            if(c<32 || c>126 || c=='/' || c=='\\' || c==':' || c=='"' || c=='*' || c=='?')return FORGE_ERROR_NAME;
        }
        if(i==64)return FORGE_ERROR_NAME;
        copy(jobName,name,sizeof(jobName));
    }
    jobCommand=command;jobError=0;
    if(command==FORGE_REFRESH || command==FORGE_REFRESH_SELECT){
        mapScanStart();if(!mapScanActive())return FORGE_ERROR_SCAN;
        jobState=FORGE_SCANNING;
    }else if(command==FORGE_RELOAD || command==FORGE_START_GAME){
        if(!(command==FORGE_START_GAME?mapStartGame():reloadMap()))return FORGE_ERROR_LOAD;jobState=FORGE_LOADING;
    }else return commandSelect();
    return 0;
}
static void commandTick(int scanResult)
{
    if(jobState==FORGE_SCANNING && !mapScanActive()){
        if(scanResult<0)commandFinish(FORGE_ERROR_SCAN);
        else if(scanResult>0){
            if(jobCommand==FORGE_REFRESH_SELECT){int error=commandSelect();if(error)commandFinish(error);}
            else commandFinish(0);
        }else commandFinish(FORGE_ERROR_CONTEXT);
    }
    if(jobState==FORGE_LOADING && mapHopResult!=1)
        commandFinish(mapHopResult==2?0:FORGE_ERROR_LOAD);
}
static void clear(void *context)
{
    (void)context;
    if(!mapStaging() || mapLoading() || mapScanActive()) return;
    mapRestoreBase(); mapSelection=-1; mapClearLoad(); mapMessage("Retail map selected");
}
/* Merge the sorted catalog with retail names without allocating a second list.
 * Keep stable catalog indices for selection, descriptions and reloads. */
static const unsigned char retailOrder[]={48,40,45,47,46,41,43,49,44,42};
static int retailPosition(int r)
{
    int c=0;const char *name=mapRetailName(retailOrder[r]);
    while(c<mapCount && mapCompareText(mapRecords[c].name,name)<0)++c;
    return c+r+1;
}
static int listEntry(int index)
{
    int r,p;if(!index)return -1;
    for(r=0;r<10;++r){p=retailPosition(r);if(index<=p)return index==p?-retailOrder[r]:index-r-1;}
    return index-11;
}
static int listFocus(void)
{
    int i,target=mapSelection<0?-(int)mapCurrentBase():mapSelection;
    for(i=0;i<mapCount+11;++i)if(listEntry(i)==target)return i;
    return 0;
}
static const char *mapLabel(void *context,int index)
{ (void)context; index=listEntry(index);return index==-1?"None":index<0?mapRetailName(-index):mapRecords[index].name; }
static void selectMap(void *context,int index)
{
    (void)context;
    if(mapScanActive())return;
    if((!mapStaging() && !(mapForgeActive() && mapHopInGame())) || !mapHooksReady() || mapLoading() || mapScanActive()) return;
    if(!index && mapHopInGame()){mapMessage("None is only available in staging");return;}
    if(index) { index=listEntry(index);if(!(index<0?chooseRetail(-index):choose((void *)(index+1)))) return; }
    else clear(0);
    menu->close();
}
static const char *mapInfo(void *context,int index)
{
    static int cached=-2;static unsigned int revision;
    static char text[292]; char author[32],description[256]; unsigned int i,p=0;
    (void)context;
    if(revision!=mapCatalogRevision){revision=mapCatalogRevision;cached=-2;}
    index=listEntry(index);
    if(index<-1)return "Retail map";
    if(index==cached) return text;
    cached=index;text[0]=0;
    if(index==-1) { copy(text,"Restore the original retail map.",sizeof(text));return text; }
    if(!mapReadInfo(&mapRecords[index],author,description) || !description[0])
        copy(description,"No description available for this map.",sizeof(description));
    if(author[0]) {
        text[p++]='b';text[p++]='y';text[p++]=' ';
        for(i=0;author[i];++i) text[p++]=author[i];
        text[p++]='\n';text[p++]='\n';
    }
    for(i=0;description[i] && p+1<sizeof(text);++i) text[p++]=description[i];
    text[p]=0;return text;
}
static int add(const char *label,int type,ModMenuCallback callback,void *context,int enabled)
{
    ModMenuItem item; int handle;
    item.label=label; item.type=type; item.value=0;
    item.minimum=item.maximum=item.step=0; item.callback=callback; item.context=context;
    handle=menu->addItem(tab,&item);
    if(handle>0 && !enabled) menu->setItemEnabled(handle,0);
    return handle>0;
}
static int lastEditable=-1;
static int rebuild(int context)
{
    int ok=1,editable=(context>0 || (mapForgeActive() && mapHopInGame())) && mapHooksReady() && !mapLoading() && !mapScanActive();
    int focused=tab>0 && menu->getActiveTab()==tab;
    lastEditable=editable;
    if(tab>0) menu->removeTab(tab);
    tab=menu->addTab("maploader","Custom Maps",UYA_MODE_BOTH);
    if(tab<0) return 0;
    menu->setTabVisibility(tab,mapForgeActive()?MODMENU_SHOW_EVERYWHERE:(MODMENU_SHOW_MAIN_MENU|MODMENU_SHOW_CAMPAIGN));
    if(mapApi->mode==UYA_MODE_CAMPAIGN) {
        ok &= add("Campaign loading backend not available",MODMENU_LABEL,0,0,1);
        ok &= add("Existing map packs require multiplayer",MODMENU_LABEL,0,0,1);
    } else {
        if(editable) {
            ok &= menu->setList(tab,mapCount+11,mapLabel,selectMap,0,listFocus())==0;
            ok &= menu->setListInfo(tab,mapInfo)==0;
        }
        else {
            ok &= add(status,MODMENU_LABEL,0,0,1);
            ok &= add("Select maps from the staging screen",MODMENU_LABEL,0,0,1);
        }
    }
    if(!ok) { menu->removeTab(tab); tab=0; return 0; }
    if(focused) menu->focusTab(tab); /* Status updates keep this tab open. */
    return 1;
}
static void deactivate(void)
{
    if(menu) {menu->setProgress(0,0,0);if(tab)menu->removeTab(tab);}
    tab=0;menu=0;dirty=1;lastContext=-1;
    commandFinish(FORGE_ERROR_INACTIVE);
    mapHopCancel();mapRestoreBase();mapDisableHooks();mapScanRelease();
}
int moduleInit(const ModuleApi *api)
{
    if(api->version<UYA_API_VERSION || api->size<sizeof(*api)) return -1;
    mapApi=api;
    api->setLifecycle(0,deactivate);
    /* Explicit opt-in for a future server without Horizon. Missing/invalid
     * configuration remains disabled; read once at boot. */
    {
        char flag=0; int fd=api->open("host0:uya/maploader-online.txt",1,0);
        if(fd>=0) { if(api->read(fd,&flag,1)==1 && flag=='1') mapAllowOnline=1; api->close(fd); }
    }
    api->publishService(MAP_HOP_SERVICE_ID,FORGE_API_VERSION,&hopService);
    mapMessage("Enter Local Play staging to scan custom maps");
    return 0;
}
void moduleUpdate(void)
{
    const ModMenuApi *found; int context,scanResult=0,scanAllowed;
    mapHopTick();
    if(mapLoading()) return; /* Hostfs/RPC may yield through the loader hook. */
    mapInstallHooks();
    context=mapStaging();
    if(mapOnlineDisabled()) {
        if(tab && menu) { menu->removeTab(tab); tab=0; }
        lastContext=-2; dirty=1; return;
    }
    /* The successful catalog stays on the multiplayer heap until deactivation. */
    if(context==1 && lastContext!=1 && !mapCatalogReady)mapScanStart();
    scanAllowed=context==1 || (mapForgeActive() && mapHopInGame());
    if(!scanAllowed && mapScanActive()){mapScanRelease();if(menu)menu->setProgress(0,0,0);}
    if(scanAllowed && mapScanActive()){
        int result=mapScanTick();scanResult=result;
        found=modmenuFind(mapApi);
        if(found)found->setProgress(mapScanActive()?"Scanning custom maps...":0,mapScanCurrent(),mapScanTotal());
        if(result)dirty=1;
    }
    commandTick(scanResult);
    if(context==1 && mapSelection<0)mapRestoreBase();
    if(context==1 && mapSelection>=0) mapChooseBase(mapRecords[mapSelection].base);
    mapUpdateName();
    if(mapApi->mode==UYA_MODE_CAMPAIGN) {
        context=-1;
        if(mapSelection>=0) { mapSelection=-1; mapClearLoad(); dirty=1; }
    }
    if(context!=lastContext) { lastContext=context; dirty=1; }
    found=modmenuFind(mapApi);
    if(!found) return;
    menu=found;
    /* Hooks may become ready after the staging screen first appears. */
    if(lastEditable!=((context>0 || (mapForgeActive() && mapHopInGame())) && mapHooksReady() && !mapLoading() && !mapScanActive())) dirty=1;
    if(dirty || !tab) { if(rebuild(context)) dirty=0; }
}
