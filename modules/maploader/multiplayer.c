/* Adapted from Horizon patch/maploader.c. This backend uses resident hostfs
 * reads; it does not install USB IRXs or depend on the patch's global state.
 * Every replaced call must still be the expected retail instruction. */
#include "maploader.h"
#define WORD(a) (*(volatile unsigned int *)(a))
#define JAL(a) (0x0c000000U | ((unsigned int)(a)>>2))
#if UYA_MODULE_REGION == 2
#define MESSAGE 0x59a970
#define MESSAGE_CALL 0x0c166a40
#define MESSAGE_LOAD 0x8c42bcc0
#define CONNECTION 0x1cd4cc
#define NAME_HOOK 0x6787a8
#define NAME_TABLE 0x352d20
#define MAP_ID 0x2412dc
#define PART_ID 0x240288
#define LEVEL_ID 0x1f83a8
#define SCENE 0x245848
#define SETTINGS 0x241640
#define TOC_HOOK 0x194f8c
#define TOC_READ 0x194ed8
#define WAD_HOOK 0x5a41c8
#define WAD_READ 0x5c2a40
#define SYNC_HOOK 0x5a4070
#define CD_SYNC 0x194a38
#define CD_READ 0x194970
#define SOUND_HOOK 0x5c1354
#define SOUND_DISC 0x193888
#define SOUND_INIT_HOOK 0x5a41f4
#define SOUND_INIT 0x5f16f0
#define SOUND_CORE_HOOK 0x5a41fc
#define SOUND_CORE 0x5c1310
#define SOUND_BUSY 0x240bc8
#define SOUND_ERROR 0x240b88
#define SOUND_RESULT 0x240140
#define SOUND_PARAMS 0x240180
#define SOUND_RPC 0x240100
#define SOUND_FLUSH 0x193590
static const unsigned int radarHooks[10]={0x4951a4,0x4972bc,0x48cb94,0x48a264,0x48967c,0x486f14,0x486f0c,0x48978c,0x488a8c,0x48840c};
static const unsigned int localRadarHooks[6]={0x4a61a4,0x4a82bc,0x49db94,0x49b264,0x49a67c,0x497f14};
#else
#define MESSAGE 0x599618
#define MESSAGE_CALL 0x0c16656a
#define MESSAGE_LOAD 0x8c42bdc0
#define CONNECTION 0x1cd64c
#define NAME_HOOK 0x675dc0
#define NAME_TABLE 0x352e20
#define MAP_ID 0x24145c
#define PART_ID 0x240408
#define LEVEL_ID 0x1f8528
#define SCENE 0x2459c8
#define SETTINGS 0x2417c0
#define TOC_HOOK 0x19507c
#define TOC_READ 0x194fc8
#define WAD_HOOK 0x5a2560
#define WAD_READ 0x5c09f8
#define SYNC_HOOK 0x5a2408
#define CD_SYNC 0x194b28
#define CD_READ 0x194a60
#define SOUND_HOOK 0x5bf36c
#define SOUND_DISC 0x193978
#define SOUND_INIT_HOOK 0x5a258c
#define SOUND_INIT 0x5eeff0
#define SOUND_CORE_HOOK 0x5a2594
#define SOUND_CORE 0x5bf328
#define SOUND_BUSY 0x240d48
#define SOUND_ERROR 0x240d08
#define SOUND_RESULT 0x2402c0
#define SOUND_PARAMS 0x240300
#define SOUND_RPC 0x240280
#define SOUND_FLUSH 0x193680
static const unsigned int radarHooks[10]={0x4931b4,0x49520c,0x48ab24,0x488274,0x48768c,0x484ea4,0x48505c,0x48789c,0x486bdc,0x48651c};
static const unsigned int localRadarHooks[6]={0x4a41b4,0x4a620c,0x49bb24,0x499274,0x49868c,0x495ea4};
#endif
int mapAllowOnline;
static MapRecord active;
static int armed,busy,failed,soundSent;
static void *soundBuffer;
static unsigned int savedSettings,savedBase,forcedBase;

typedef void (*SoundCallback)(int,unsigned long long);

int mapMultiplayerReady(void)
{
    return mapApi->mode==UYA_MODE_MULTIPLAYER &&
        WORD(0x12ae40)==0x27bdff40 && WORD(0x12b080)==0x27bdff20;
}
/* The lobby connection becomes non-null before online staging. */
int mapOnlineDisabled(void)
{ return !mapAllowOnline && mapMultiplayerReady() && WORD(CONNECTION)!=0; }
static int hookedName(void)
{
    unsigned int level=WORD(MAP_ID),i;
    if(!mapOnlineDisabled() && mapSelection>=0 && mapSelection<mapCount &&
       (level==mapRecords[mapSelection].base || level==mapRecords[mapSelection].base+10)) {
        char *dest=(char *)(NAME_TABLE+((level-40)%10)*32);
        for(i=0;i<32;++i) dest[i]=mapRecords[mapSelection].name[i];
    }
    return 1;
}
int mapStaging(void)
{
    unsigned int i,local,online;
    if(!mapMultiplayerReady() || WORD(LEVEL_ID)!=39 || WORD(SCENE)!=1) return 0;
    local=WORD(0x1c5c064+23*4); online=WORD(0x1c5c064+33*4);
    for(i=0;i<3;++i) {
        unsigned int menu=WORD(0x1c5c110+i*4);
        if(menu && menu==local) return 1;
        if(menu && menu==online) return 2;
    }
    return 0;
}
/* Resolve the retail name from GameLevel, never from a label that may already
 * contain a custom name (including after a live module reload). */
#ifndef MAP_MESSAGE
#define MAP_MESSAGE(id) ((const char *(*)(int))MESSAGE)(id)
#endif
static const char *retailMapName(void)
{
    static const unsigned int ids[10]={0xbb1,0xbb2,0xda,0xbb4,0xd8,0xd4,0x18c4,0x18c5,0x18c6,0x18c7};
    static const unsigned int signature[16]={0x27bdfff0,0xffbf0000,MESSAGE_CALL,0,
        0x0040182d,0x2782aaf8,0x04600005,0x00031900,0x3c020025,MESSAGE_LOAD,
        0x00621821,0x8c620000,0xdfbf0000,0x03e00008,0x27bd0010,0};
    unsigned int settings=WORD(SETTINGS),base,i;
    if(settings<0x100000 || settings>0x1fffe00 || (settings&3)) return 0;
    base=WORD(settings+0x17c);if(base<40 || base>49) return 0;
    for(i=0;i<16;++i) if(WORD(MESSAGE+i*4)!=signature[i]) return 0;
    return MAP_MESSAGE(ids[base-40]);
}
/* Local and online staging both use child 1 for the map name. */
static void writeStagingName(const char *name)
{
    unsigned int menu,child,i;int context=mapStaging();
    if(!name || !context) return;
    menu=WORD(0x1c5c064+(context==1?23:33)*4);
    if(menu<0x100000 || menu>0x1fffd00 || (menu&3)) return;
    if(WORD(menu+0x10c)<2 || WORD(menu+0x10c)>96) return;
    child=WORD(menu+0x114);
    if(child<0x100000 || child>0x1ffff80 || (child&3)) return;
    for(i=0;i<31 && name[i];++i) ((volatile char *)child)[0x14+i]=name[i];
    for(;i<32;++i) ((volatile char *)child)[0x14+i]=0;
}
void mapUpdateName(void)
{
    if(!mapStaging() || mapOnlineDisabled()) return;
    writeStagingName(mapSelection>=0 && mapSelection<mapCount?
                     mapRecords[mapSelection].name:retailMapName());
}
int mapChooseBase(unsigned int base)
{
    unsigned int settings;
    /* Local staging has a different UI, but its callback reads this same
     * settings pointer and the halfword at +0x17c. Verified in the local
     * staging disassembly and an NTSC-U live lobby. The game chooses the
     * appropriate online/local overlay when starting the match.
     * Online settings require host/server synchronization, not a local write. */
    if(mapStaging()!=1 || base<40 || base>49) return 0;
    settings=WORD(SETTINGS);
    if(settings<0x100000 || settings>0x1fffe00 || (settings&3) ||
       (int)WORD(settings+0x174)>0 || WORD(settings+0x17c)<40 || WORD(settings+0x17c)>49) return 0;
    if(savedSettings!=settings) { savedSettings=settings; savedBase=WORD(settings+0x17c); }
    WORD(settings+0x17c)=forcedBase=base;
    return 1;
}
void mapRestoreBase(void)
{
    if(mapStaging()==1 && savedSettings && WORD(SETTINGS)==savedSettings &&
       WORD(savedSettings+0x17c)==forcedBase && (int)WORD(savedSettings+0x174)<=0)
        WORD(savedSettings+0x17c)=savedBase;
    savedSettings=0;
    /* Look up the name after restoring the ID, even if nothing was cached. */
    if(mapStaging()) writeStagingName(retailMapName());
}
int mapLoading(void) { return busy || failed; }
void mapClearLoad(void) { if(!busy) { armed=0; failed=0; soundSent=0; soundBuffer=0; } }

static void loadError(void)
{
    failed=1;
    mapMessage("Map read failed: fix files and restart");
    mapApi->print("maploader: load stopped; refusing to run partially replaced map data\n");
}
static int writable(void *dest,unsigned int size)
{
    unsigned int address=(unsigned int)dest;
    /* Only the game's allocated EE data buffers, never module/kernel memory. */
    return address>=0x100000 && address<0x2000000 && size<=0x2000000-address;
}
static void hookedTable(unsigned int sector,unsigned int count,void *dest)
{
    unsigned int level=WORD(MAP_ID);
    ((void (*)(unsigned int,unsigned int,void *))TOC_READ)(sector,count,dest);
    armed=failed=soundSent=0; soundBuffer=0;
    if(mapSelection<0 || mapSelection>=mapCount) return;
    active=mapRecords[mapSelection];
    /* Local play loads 50..59, online play 40..49. Never reuse a selected
     * multiplayer WAD for campaign/menu or a different base map's code. */
    if(level!=active.base && level!=active.base+10) return;
    busy=1;
    if(!mapValidate(&active)) {
        busy=0; mapMessage("Files changed; re-index before selecting"); return;
    }
    busy=0; armed=1;
    if(active.wad) ((unsigned int *)dest)[5]=(active.wad+0x7ff)/0x800;
    if(active.world) ((unsigned int *)dest)[9]=(active.world+0x7ff)/0x800;
    mapApi->print("maploader: replacing map %u with %s\n",level,active.filename);
}
static int soundFromEE(void *buffer)
{
    int result,spins=0;
    if(WORD(SOUND_BUSY)) return -1;
    WORD(SOUND_ERROR)=0; WORD(SOUND_RESULT)=0xffffffff; WORD(SOUND_PARAMS)=(unsigned int)buffer;
    ((void (*)(void *,void *))0x126b28)((void *)SOUND_RESULT,(void *)(SOUND_RESULT+4));
    while(((int (*)(void *))0x12a018)((void *)SOUND_RPC)) {
        if(++spins>1000000) return -1;
        ((void (*)(void))SOUND_FLUSH)(); mapApi->flushCache(0);
    }
    WORD(SOUND_BUSY)=1;
    result=((int (*)(void *,int,int,void *,int,void *,int,void *,void *))0x129e18)
        ((void *)SOUND_RPC,0x57,1,(void *)SOUND_PARAMS,4,(void *)SOUND_RESULT,4,0,0);
    if(result<0) { WORD(SOUND_ERROR)=0x106; return -1; }
    spins=0;
    do {
        mapApi->flushCache(0);
        result=(int)WORD(SOUND_RESULT);
        if(++spins>1000000) return -1;
    } while(result==-1);
    return result;
}
static void hookedSound(int loc,int offset,SoundCallback callback,unsigned long long user)
{
    if(armed && soundBuffer) {
        int result;
        if(soundSent) return;
        WORD(SOUND_BUSY+8)=(unsigned int)callback;
        *(volatile unsigned long long *)(SOUND_BUSY+16)=user;
        result=soundFromEE(soundBuffer);
        if(result<0) { loadError(); return; }
        if(callback) callback(result,user);
        soundSent=1; return;
    }
    ((void (*)(int,int,SoundCallback,unsigned long long))SOUND_DISC)(loc,offset,callback,user);
}
static void hookedSoundInit(void)
{ if(!armed || !soundSent) ((void (*)(void))SOUND_INIT)(); }
static void hookedSoundCore(void)
{ if(!armed || !soundSent) ((void (*)(void))SOUND_CORE)(); }
static void hookedLoad(void *dest,unsigned int sector,unsigned int sectors)
{
    unsigned int part=WORD(PART_ID),size;
    const char *extension;
    if(!armed || part>1) {
        ((void (*)(void *,unsigned int,unsigned int))WAD_READ)(dest,sector,sectors); return;
    }
    size=part?active.world:active.wad; extension=part?".world":".wad";
    busy=1;
    if(part==0 && active.sound && !soundSent) {
        /* Sound is staged in the level buffer and uploaded before the WAD
         * replaces it, matching Horizon's load ordering. */
        if(active.sound>sectors*0x800 || !writable(dest,active.sound) ||
           !mapReadPart(&active,".sound",dest,active.sound)) { loadError(); busy=0; return; }
        soundBuffer=dest; mapApi->flushCache(0);
        ((void (*)(void))SOUND_INIT)();
        ((void (*)(void))SOUND_CORE)();
        soundBuffer=0;
        if(!soundSent || failed) { loadError(); busy=0; return; }
    }
    if(size) {
        if(sectors>0x4000 || size>sectors*0x800 || !writable(dest,sectors*0x800) ||
           !mapReadPart(&active,extension,dest,size)) { loadError(); busy=0; return; }
        /* Disc reads are sector padded; file payloads need not be. */
        {
            unsigned char *p=(unsigned char *)dest+size;
            unsigned int remaining=sectors*0x800-size;
            while(remaining--) *p++=0;
        }
        mapApi->flushCache(0);
    } else {
        ((void (*)(void *,unsigned int,unsigned int))WAD_READ)(dest,sector,sectors);
    }
    busy=0;
}
static int hookedSync(int mode)
{
    int result=((int (*)(int))CD_SYNC)(mode);
    return failed?1:result;
}
static int hookedRadar(unsigned int sector,unsigned int count,void *dest,void *args)
{
    if(armed && active.radar && count==0x21 && writable(dest,0x10800)) {
        int ok; busy=1;
        ok=mapReadPart(&active,".map",dest,active.radar); busy=0;
        if(ok) { mapApi->flushCache(0); return 0; }
    }
    return ((int (*)(unsigned int,unsigned int,void *,void *))CD_READ)(sector,count,dest,args);
}
struct Hook { unsigned int address,original; void *replacement; };
static struct Hook hooks[]={
    {TOC_HOOK,JAL(TOC_READ),hookedTable},
    {WAD_HOOK,JAL(WAD_READ),hookedLoad},
    {SYNC_HOOK,JAL(CD_SYNC),hookedSync},
    {SOUND_HOOK,JAL(SOUND_DISC),hookedSound},
    {SOUND_INIT_HOOK,JAL(SOUND_INIT),hookedSoundInit},
    {SOUND_CORE_HOOK,JAL(SOUND_CORE),hookedSoundCore}
};
static int radarMatches(unsigned int address)
{
    static const unsigned int signature[10]={
        0x0260202d,0xae000018,0xae00001c,0x0220282d,0x0240302d,
        JAL(CD_READ),0x26070040,0x10400007,0x24020001,0xae120014
    };
    unsigned int i;
    for(i=0;i<10;++i) if(WORD(address-20+i*4)!=signature[i]) return 0;
    return 1;
}
int mapHooksReady(void)
{
    unsigned int i;
    if(!mapMultiplayerReady() || mapOnlineDisabled()) return 0;
    for(i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)
        if(WORD(hooks[i].address)!=JAL(hooks[i].replacement)) return 0;
    return 1;
}
void mapInstallHooks(void)
{
    unsigned int i,changed=0,level;
    if(!mapMultiplayerReady() || busy) return;
    if(mapOnlineDisabled()) {
        /* Restore only instructions still owned by this module. A competing
         * patch must never be replaced with our saved retail instructions. */
        for(i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)
            if(WORD(hooks[i].address)==JAL(hooks[i].replacement)) {
                WORD(hooks[i].address)=hooks[i].original; changed=1;
            }
        if(WORD(NAME_HOOK)==(0x08000000U|((unsigned int)hookedName>>2)) &&
           WORD(NAME_HOOK+4)==0x27bd0050) { WORD(NAME_HOOK)=0x03e00008; changed=1; }
        level=WORD(LEVEL_ID);
        if(level>=40 && level<=55) {
            unsigned int address=level<50?radarHooks[level-40]:localRadarHooks[level-50];
            if(WORD(address)==JAL(hookedRadar) && WORD(address+4)==0x26070040) {
                WORD(address)=JAL(CD_READ); changed=1;
            }
        }
        mapSelection=-1; mapClearLoad(); savedSettings=0;
        if(changed) { mapApi->flushCache(0); mapApi->flushCache(2); }
        return;
    }
    if(WORD(LEVEL_ID)!=39) savedSettings=0;
    /* Tail-hook the transition name builder, preserving its stack delay slot. */
    if(WORD(NAME_HOOK)==0x03e00008 && WORD(NAME_HOOK+4)==0x27bd0050 &&
       WORD(NAME_HOOK-4)==0xdfbf0048 && WORD(NAME_HOOK-8)==0xdfbe0040) {
        WORD(NAME_HOOK)=0x08000000U|((unsigned int)hookedName>>2); changed=1;
    }
    /* Install the loading group atomically: never combine our loader with a
     * Horizon/other mod's half-owned hook set. Overlay reloads restore retail
     * calls; accept either retail or our own instruction on reinstallation. */
    for(i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i) {
        unsigned int instruction=WORD(hooks[i].address);
        if(instruction!=hooks[i].original && instruction!=JAL(hooks[i].replacement)) break;
    }
    if(i==sizeof(hooks)/sizeof(hooks[0])) {
        for(i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i) {
            unsigned int instruction=JAL(hooks[i].replacement);
            if(WORD(hooks[i].address)!=instruction) { WORD(hooks[i].address)=instruction; changed=1; }
        }
    }
    level=WORD(LEVEL_ID);
    /* Local radar hooks were separately signature-matched in code5 levels
     * 50..55. There are no split-screen counterparts for online bases 46..49. */
    if(armed && ((level>=40 && level<=49) || (level>=50 && level<=55))) {
        unsigned int address=level<50?radarHooks[level-40]:localRadarHooks[level-50];
        if(radarMatches(address)) { WORD(address)=JAL(hookedRadar); changed=1; }
    }
    if(changed) { mapApi->flushCache(0); mapApi->flushCache(2); }
}
