/* Standalone Custom Maps tab. Map selection is independent of menu internals;
 * file IO and game hooks live in files.c and multiplayer.c respectively. */
#include "include/modmenu.h"
#include "maploader.h"
static const ModMenuApi *menu;
static int tab,dirty=1,lastContext=-1,catalogLoaded;
static char status[48]="Choose a map in multiplayer staging";

static void copy(char *dest,const char *src,unsigned int cap)
{ unsigned int i=0; while(src[i] && i+1<cap) { dest[i]=src[i]; ++i; } dest[i]=0; }
void mapMessage(const char *message) { copy(status,message,sizeof(status)); dirty=1; }
static int choose(void *context)
{
    int index=(int)context-1;
    if(!mapStaging() || !mapHooksReady() || mapLoading() || index<0 || index>=mapCount) return 0;
    if(!mapValidate(&mapRecords[index])) { mapMessage("Map files changed: rebuild the catalog"); return 0; }
    mapSelection=index;
    mapApi->print("maploader: selected %s; choose retail base ID %u (local ID %u) before starting\n",
                  mapRecords[index].name,mapRecords[index].base,mapRecords[index].base+10);
    mapMessage(mapChooseBase(mapRecords[index].base)?"Selected; local base map updated":
               "Selected; host must choose its retail base");
    return 1;
}
static void clear(void *context)
{
    (void)context;
    if(!mapStaging() || mapLoading()) return;
    mapRestoreBase(); mapSelection=-1; mapClearLoad(); mapMessage("Retail map selected");
}
static const char *mapLabel(void *context,int index)
{ (void)context; return index?mapRecords[index-1].name:"None"; }
static void selectMap(void *context,int index)
{
    (void)context;
    if(!mapStaging() || !mapHooksReady() || mapLoading()) return;
    if(index) { if(!choose((void *)index)) return; }
    else clear(0);
    menu->close();
}
static const char *mapInfo(void *context,int index)
{
    static int cached=-1;
    static char text[336]; char author[32],description[256]; unsigned int i,p=0;
    (void)context;
    if(index==cached) return text;
    cached=index;text[0]=0;
    if(!index) { copy(text,"Use the selected retail map without a custom map override.",sizeof(text));return text; }
    if(!mapReadInfo(&mapRecords[index-1],author,description) || !description[0])
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
static int rebuild(int context)
{
    int ok=1,editable=context>0 && mapHooksReady() && !mapLoading();
    int focused=tab>0 && menu->getActiveTab()==tab;
    if(tab>0) menu->removeTab(tab);
    tab=menu->addTab("maploader","Custom Maps",UYA_MODE_BOTH);
    if(tab<0) return 0;
    if(mapApi->mode==UYA_MODE_CAMPAIGN) {
        ok &= add("Campaign loading backend not available",MODMENU_LABEL,0,0,1);
        ok &= add("Existing map packs require multiplayer",MODMENU_LABEL,0,0,1);
    } else {
        if(editable && mapCount) {
            ok &= menu->setList(tab,mapCount+1,mapLabel,selectMap,0,mapSelection+1)==0;
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
    if(menu && tab) menu->removeTab(tab);
    tab=0;menu=0;dirty=1;lastContext=-1;
    mapRestoreBase();mapDisableHooks();
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
    catalogLoaded=mapLoadCatalog();
    if(!catalogLoaded) mapMessage("No catalog: run tools/index_maps.py");
    api->print("maploader: %d maps indexed; select from Custom Maps in staging\n",mapCount);
    return 0;
}
void moduleUpdate(void)
{
    const ModMenuApi *found; int context;
    if(mapLoading()) return; /* Hostfs/RPC may yield through the loader hook. */
    mapInstallHooks();
    context=mapStaging();
    if(mapOnlineDisabled()) {
        if(tab && menu) { menu->removeTab(tab); tab=0; }
        lastContext=-2; dirty=1; return;
    }
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
    if(dirty || !tab) { if(rebuild(context)) dirty=0; }
}
