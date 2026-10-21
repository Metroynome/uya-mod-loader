/* Development-only Local Play controls. Map Loader owns all transition work. */
#include "include/forge.h"
#include "include/modmenu.h"
static const ModuleApi *loader;
static const ModMenuApi *menu;
static int enabled,tab,item,lastReady=-1;
static int active(void) {return enabled && loader->context==UYA_CONTEXT_LOCAL;}
static const ForgeApi service={FORGE_API_VERSION,sizeof(ForgeApi),active};
static const MapHopApi *hop(void)
{
    const MapHopApi *p=loader->findService(MAP_HOP_SERVICE_ID,FORGE_API_VERSION);
    return p && p->version==FORGE_API_VERSION && p->size>=sizeof(*p)?p:0;
}
static void reload(void *unused)
{
    const MapHopApi *p=hop();(void)unused;
    if(p && p->reload()>0 && menu)menu->close();
}
static void activate(void) {enabled=1;}
static void deactivate(void)
{
    enabled=0;
    if(menu && tab>0)menu->removeTab(tab);
    menu=0;tab=0;lastReady=-1;
}
int moduleInit(const ModuleApi *api)
{
    loader=api;
    if(api->version<UYA_API_VERSION || api->size<sizeof(*api))return -1;
    api->restrictContexts(UYA_CONTEXT_LOCAL);
    api->setLifecycle(activate,deactivate);enabled=1;
    return api->publishService(FORGE_SERVICE_ID,FORGE_API_VERSION,&service);
}
void moduleUpdate(void)
{
    const MapHopApi *p=hop();int ready=p && p->available();
    static const ModMenuItem action={"Reload Current Map",MODMENU_ACTION,0,0,0,0,reload,0};
    if(!menu)menu=modmenuFind(loader);
    if(!menu)return;
    if(tab<=0){
        tab=menu->addTab("forge","Forge",UYA_MODE_MULTIPLAYER);if(tab<=0)return;
        menu->setTabContexts(tab,UYA_CONTEXT_LOCAL);
        item=menu->addItem(tab,&action);lastReady=-1;
    }
    if(lastReady!=ready){menu->setItemEnabled(item,ready);lastReady=ready;}
}

void *memset(void *dest,int value,unsigned int size)
{unsigned char *p=dest;while(size--)*p++=(unsigned char)value;return dest;}
