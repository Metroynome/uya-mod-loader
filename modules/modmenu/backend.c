/* Small shared campaign/MP backend. The font ABI is the same in the verified
 * overlays; addresses are generated from the extracted code, never guessed.
 * No network globals, server RPCs, or Horizon patch state are required. */
#include "module.h"
#include "backend.h"
#include "renderers.h"
#include "pause.h"
#include "campaign-pause.h"
#include "players.h"
#ifndef MENU_FLUSH
#define MENU_FLUSH(operation) ((void (*)(int))0x1264e0)(operation)
#endif
/* Regional font differences are verified by the renderer-table generator. */
static unsigned int rendererFont(const struct MenuRenderer *r)
{
#if UYA_MODULE_REGION == 3
    return r->print-(r->level>=39?1720:1696);
#else
    return r->print-1688;
#endif
}
static unsigned int rendererFontHash(const struct MenuRenderer *r)
{
#if UYA_MODULE_REGION == 3
    return r->level>=39?0x13484292U:0xbe9688deU;
#elif UYA_MODULE_REGION == 2
    return r->level>=39?0x899b90b4U:0xc72973b6U;
#else
    return r->level>=39?0x84926133U:0xc72973b6U;
#endif
}
static const struct MenuRenderer *active;
static volatile unsigned char *pad;
static int levelId;
static unsigned int inputHook,inputOriginal;
static void (*pauseOpen)(void);
/* Campaign: intercept each fresh poll before retail code builds gameplay input. Keep a
 * private physical sample for menu navigation; the game receives neutral input.
 * The raw callback ABI is shared by librac3 and libuya's PAD structure. */
typedef void (*RawPadCallback)(void *,unsigned char *,int,void *);
static volatile unsigned char *capturedPad,*capturedRaw;
static RawPadCallback previousRawCallback;
static unsigned char menuSample[32];
static void captureRawPad(void *controller,unsigned char *sample,int size,void *context)
{
    int i;
    for(i=0;i<32;++i) menuSample[i]=i<size?sample[i]:0;
    for(i=2;i<size;++i) sample[i]=i<4?255:i<8?127:0;
    /* Multiplayer's callback records input into the player stream. It must
     * receive the neutral sample too, otherwise camera/zoom still leak. */
    if(previousRawCallback) previousRawCallback(controller,sample,size,context);
}
static void clearProcessedPad(volatile unsigned char *pad)
{
    unsigned int i;
    for(i=0x100;i<0x180;i+=4) *(volatile unsigned int *)(pad+i)=0;
    for(i=0x1a0;i<0x1cc;i+=4) *(volatile unsigned int *)(pad+i)=0;
    /* Buffered/lagged input must not replay a shot or jump after capture. */
    for(i=0x1e0;i<0x34c;i+=4) *(volatile unsigned int *)(pad+i)=0;
    for(i=0x354;i<0x54c;i+=4) *(volatile unsigned int *)(pad+i)=0;
}
static unsigned int hash(const volatile unsigned char *p,unsigned int size)
{ unsigned int h=2166136261U; while(size--) h=(h^*p++)*16777619U; return h; }
int menuBackendReady(unsigned int mode)
{
    unsigned int i; const struct MenuRenderer *r;
    active=0;
    if(mode==UYA_MODE_CAMPAIGN) {
#if UYA_MODULE_REGION == 3
        levelId=*(volatile int *)0x1de75c; pad=(void *)0x1d6380;
#else
        levelId=*(volatile int *)0x1d545c; pad=(void *)0x1cd0c0;
#endif
    } else if(mode==UYA_MODE_MULTIPLAYER) {
#if UYA_MODULE_REGION == 2
        levelId=*(volatile int *)0x1f83a8; pad=(void *)0x225800;
        if(*(volatile int *)0x245848!=1) return 0;
#elif UYA_MODULE_REGION == 1
        levelId=*(volatile int *)0x1f8528; pad=(void *)0x225980;
        if(*(volatile int *)0x2459c8!=1) return 0;
#else
        return 0; /* Loader does not support the unverified JP MP layout. */
#endif
    } else return 0;
    for(i=0;i<sizeof(renderers)/sizeof(renderers[0]);++i) {
        r=&renderers[i]; if(r->level!=levelId) continue;
        /* Generator verifies these common hashes and the width offset for every row. */
        if(hash((void *)r->print,48)!=0x1f803f0eU || hash((void *)rendererFont(r),44)!=rendererFontHash(r) ||
           hash((void *)r->box,64)!=r->boxHash || hash((void *)r->context,36)!=r->contextHash || hash((void *)(r->print-0x2a0),64)!=0xa344514dU) return 0;
        active=r; return 1;
    }
    return 0;
}
int menuBackendLevel(void) { return levelId; }
unsigned int menuBackendButtons(unsigned int mode)
{
    const volatile unsigned char *sample=capturedRaw==pad?menuSample:pad;
    (void)mode;
    if(!active || sample[0]!=0 || (sample[1]!=0x41 && sample[1]!=0x73 && sample[1]!=0x79)) return 0;
    return (~(sample[2]|(sample[3]<<8)))&0xffffU;
}
/* Match libuya padDisableInput: suppress every local/split-screen player,
 * including players whose input does not come from the menu controller.
 * Keep longer retail locks intact; the short timer expires after release. */
static volatile unsigned char *captureLocalPlayers(unsigned int mode)
{
#if UYA_MODULE_REGION != 3
    unsigned int i,j; volatile unsigned char *controller=0;
    if(mode!=UYA_MODE_MULTIPLAYER) return 0;
    for(i=0;i<sizeof(menuPlayers)/sizeof(menuPlayers[0]);++i) {
        const struct MenuPlayers *p=&menuPlayers[i];
        const volatile unsigned int *ref=(void *)p->reference;
        if(p->level!=levelId) continue;
        /* Verify this overlay still references the expected player array. */
        if(ref[0]!=0x27bdff90 || ref[1]!=(0x3c020000|((p->array+0x8000)>>16)) ||
           ref[2]!=(0x24420000|(p->array&65535)) || ref[3]!=0xffb10038) return 0;
        for(j=0;j<8;++j) {
            unsigned int player=((volatile unsigned int *)p->array)[j];
            volatile short *timer;
            if(player<0x100000 || player>0x1ffd000 || (player&15)) continue;
            if(!*(volatile unsigned char *)(player+0x1a14)) continue;
            /* P1_PAD is the raw/history sample at PAD + 0x80 in MP.
             * Require the local player's own pPad to confirm the structure. */
            if(*(volatile unsigned int *)(player+0x2534)==(unsigned int)(pad-0x80))
                controller=pad-0x80;
            timer=(void *)(player+0x320); /* HeroTimers at 0x2f0 + noInput 0x30 */
            if(*timer<3) *timer=3;
        }
        return controller;
    }
#else
    (void)mode;
#endif
    return 0;
}
void menuBackendCapture(unsigned int mode)
{
    unsigned int i,address=0,original=0;
    volatile unsigned char *controller;
    if(!active) return;
    controller=mode==UYA_MODE_CAMPAIGN?pad:captureLocalPlayers(mode);
    if(controller && !capturedPad) {
        for(i=0;i<32;++i) menuSample[i]=pad[i];
        previousRawCallback=*(RawPadCallback volatile *)(controller+0x56c);
        capturedPad=controller;capturedRaw=pad;
        *(RawPadCallback volatile *)(controller+0x56c)=captureRawPad;
    }
    /* Front-end navigation uses the live raw sample and lobby patch. Match
     * capture uses only a PAD confirmed by the local player's pPad pointer. */
    if(controller) clearProcessedPad(controller);
    /* Horizon's lobby input dispatcher also reads raw input. Suppress its
     * event result while open, restoring only the instruction we own. */
    if(mode==UYA_MODE_MULTIPLAYER && levelId==39) {
#if UYA_MODULE_REGION == 2
        address=0x686300; original=0x0c1a184e;
#elif UYA_MODULE_REGION == 1
        address=0x6837e0; original=0x0c1a0d86;
#endif
    }
    if(address && !inputHook && *(volatile unsigned int *)address==original) {
        inputHook=address; inputOriginal=original;
        *(volatile unsigned int *)address=0x24020000;
        MENU_FLUSH(0); MENU_FLUSH(2);
    }
}
void menuBackendRelease(void)
{
    if(capturedPad) {
        if(*(RawPadCallback volatile *)(capturedPad+0x56c)==captureRawPad)
            *(RawPadCallback volatile *)(capturedPad+0x56c)=previousRawCallback;
        capturedPad=0;capturedRaw=0;previousRawCallback=0;
    }
    if(inputHook) {
        /* The delay slot identifies the same lobby call site even if an
         * overlay was replaced while closing the menu. */
        if(*(volatile unsigned int *)inputHook==0x24020000 &&
           *(volatile unsigned int *)(inputHook+4)==0x0240202d) {
            *(volatile unsigned int *)inputHook=inputOriginal;
            if(*(volatile unsigned int *)0x1264e0==0x24030064) {
                MENU_FLUSH(0); MENU_FLUSH(2);
            }
        }
        inputHook=inputOriginal=0;
    }
}
void menuBackendBox(float x,float y,float width,float height,unsigned int color)
{
    unsigned int buffer[25] __attribute__((aligned(16)))={0};
    float crop[4] __attribute__((aligned(16)))={0,0,1,1};
    union { float f; unsigned int u; } value;
    if(!active) return;
    buffer[0]=1; buffer[2]=active->context; buffer[3]=0x9e0d;
    value.f=x+width*.5f; buffer[4]=value.u;
    value.f=y+height*.5f; buffer[5]=value.u;
    value.f=width; buffer[6]=value.u;
    value.f=height; buffer[7]=value.u;
    buffer[10]=0x40000000;
    buffer[14]=buffer[15]=buffer[16]=buffer[17]=color; buffer[19]=10;
    ((void (*)(void *,void *))active->box)(buffer,crop);
}
void menuBackendText(float x,float y,unsigned int color,const char *text)
{
    typedef int (*Print)(float,float,float,float,float,float,unsigned int,const char *,unsigned long long,unsigned long long,int,unsigned int);
    float height;
    if(!active) return;
#if UYA_MODULE_REGION == 2
    height=448;
#else
    height=416;
#endif
    ((void (*)(int))rendererFont(active))(0);
    ((Print)active->print)(x*512,y*height,.7f,.7f,1,1,color,text,~0ULL,0,1,0x80000000);
}

/* Campaign has no Continue row. Preserve every retail option and append a
 * new row to private copies of its three main-menu table variants. */
static unsigned int campaignTables[3][50],campaignSources[3];
static const struct CampaignPause *campaignActive;
static void (*campaignOpen)(void);
static void campaignMenuSelected(void)
{
    void *parent;
    if(!campaignActive || !campaignOpen) return;
    parent=((void *(*)(void))campaignActive->parent)();
    ((void (*)(void *))campaignActive->push)(parent);
    /* The retail dispatcher pops this pause screen after our callback because
     * the appended row has the same close-after-selection flag as Options. */
    campaignOpen();
}
#define PAUSE_WORD(a) (*(volatile unsigned int *)(a))
#if UYA_MODULE_REGION == 3
#define CAMPAIGN_SHIFT (-4)
#else
#define CAMPAIGN_SHIFT 0
#endif
static void campaignPause(void (*openMenu)(void))
{
    static const unsigned int hi[3]={0x78,0xb8,0xd4},lo[3]={0x80,0xc0,0xdc},counts[3]={9,9,8};
    const struct CampaignPause *p=0;
    unsigned int i,j,setup,installed=1;char *label;
    if(!active || levelId<0 || levelId>=39) return;
    for(i=0;i<sizeof(campaignPauses)/sizeof(campaignPauses[0]);++i)
        if(campaignPauses[i].level==levelId) { p=&campaignPauses[i];break; }
    if(!p || hash((void *)p->message,64)!=p->messageHash) return;
    setup=p->setup+CAMPAIGN_SHIFT;
    for(i=0;i<3;++i) {
        unsigned int addr=(unsigned int)campaignTables[i];
        if(PAUSE_WORD(setup+hi[i])!=(0x3c020000|((addr+0x8000)>>16)) ||
           PAUSE_WORD(setup+lo[i])!=(0x24420000|(addr&0xffff)) ||
           PAUSE_WORD(setup+lo[i]+4)!=(0x24030000|(counts[i]+1))) installed=0;
    }
    if(installed && campaignActive!=p) return;
    if(!installed) {
        if(hash((void *)p->setup,0x100)!=p->setupHash) return;
        for(i=0;i<3;++i) {
            unsigned int address=(PAUSE_WORD(setup+hi[i])&0xffff)<<16;
            address+=(short)(PAUSE_WORD(setup+lo[i])&0xffff);
            if(address<0x100000 || address>0x1fff000 || (address&3)) return;
            campaignSources[i]=address;
        }
        label=((char *(*)(int))p->message)(0x1115);
        if((unsigned int)label<0x100000 || (unsigned int)label>0x1fffff0) return;
        for(j=0;j<8 && label[j];++j) {}
        if(j<8) return;
        for(j=0;j<9;++j) label[j]="MOD MENU"[j];
    }
    campaignActive=p;campaignOpen=openMenu;
    for(i=0;i<3;++i) {
        unsigned int *row=campaignTables[i]+counts[i]*5;
        /* Retail code can change row availability after obtaining gadgets. */
        for(j=0;j<counts[i]*5;++j) campaignTables[i][j]=PAUSE_WORD(campaignSources[i]+j*4);
        row[0]=0xd0015;row[1]=0x1115;row[2]=1;
        row[3]=(unsigned int)campaignMenuSelected;row[4]=1;
    }
    if(installed) return;
    for(i=0;i<3;++i) {
        unsigned int address=(unsigned int)campaignTables[i];
        PAUSE_WORD(setup+hi[i])=0x3c020000|((address+0x8000)>>16);
        PAUSE_WORD(setup+lo[i])=0x24420000|(address&0xffff);
        PAUSE_WORD(setup+lo[i]+4)=0x24030000|(counts[i]+1);
    }
    /* The normal variant can enter through either of two lui instructions. */
    PAUSE_WORD(setup+0xa4)=PAUSE_WORD(setup+0xd4);
    ((void (*)(int))0x11f0a0)(0);((void (*)(int))0x11f0a0)(2);
}

/* Same Continue tail-hook as Horizon main.c, separately verified for local
 * overlays. The retail callback closes the pause screen before opening us. */
void menuBackendPause(unsigned int mode,void (*openMenu)(void))
{
    pauseOpen=openMenu;
    if(mode==UYA_MODE_CAMPAIGN) { campaignPause(openMenu);return; }
#if UYA_MODULE_REGION != 3
    unsigned int i,j;
    if(!active || mode!=UYA_MODE_MULTIPLAYER) return;
    for(i=0;i<sizeof(pauseEntries)/sizeof(pauseEntries[0]);++i) {
        const struct PauseEntry *p=&pauseEntries[i]; char *label;
        unsigned int jump=0x08000000U|((unsigned int)openMenu>>2);
        volatile unsigned int *entry=(void *)p->entry,*tail=(void *)(p->callback+0x54);
        if(p->level!=levelId) continue;
        if(entry[2]!=p->callback || entry[1]!=1 ||
           (entry[0]!=0x2cc && entry[0]!=0x1115) ||
           (*tail!=0x03e00008 && *tail!=jump) || tail[1]!=0x27bd0010 ||
           hash((void *)p->callback,84)!=p->callbackHash ||
           hash((void *)p->message,64)!=p->messageHash) return;
        if(*tail==jump && entry[0]==0x1115) return;
        label=((char *(*)(int))p->message)(0x1115);
        if((unsigned int)label<0x100000 || (unsigned int)label>0x1fffff0) return;
        /* The spare city string must have room for the replacement. */
        for(j=0;j<8 && label[j];++j) {}
        if(j<8) return;
        for(j=0;j<9;++j) label[j]="MOD MENU"[j];
        entry[0]=0x1115;*tail=jump;MENU_FLUSH(0);MENU_FLUSH(2);
        return;
    }
#else
    (void)mode;(void)openMenu;
#endif
}

/* Undo only still-owned pause entries before this module stops updating. */
void menuBackendDeactivate(void)
{
    unsigned int i,changed=0;
    menuBackendRelease();
#if UYA_MODULE_REGION != 3
    for(i=0;i<sizeof(pauseEntries)/sizeof(pauseEntries[0]);++i) {
        const struct PauseEntry *p=&pauseEntries[i];
        unsigned int jump=0x08000000U|((unsigned int)pauseOpen>>2);
        if(PAUSE_WORD(p->callback+0x54)==jump && PAUSE_WORD(p->callback+0x58)==0x27bd0010 &&
           hash((void *)p->callback,84)==p->callbackHash) {
            PAUSE_WORD(p->callback+0x54)=0x03e00008;
            if(PAUSE_WORD(p->entry)==0x1115 && PAUSE_WORD(p->entry+8)==p->callback)
                PAUSE_WORD(p->entry)=0x2cc;
            changed=1;
        }
    }
    if(changed && PAUSE_WORD(0x1264e0)==0x24030064) { MENU_FLUSH(0);MENU_FLUSH(2); }
#endif
    if(campaignActive) {
        static const unsigned int hi[3]={0x78,0xb8,0xd4},lo[3]={0x80,0xc0,0xdc},count[3]={9,9,8};
        unsigned int setup=campaignActive->setup+CAMPAIGN_SHIFT;changed=0;
        for(i=0;i<3;++i) {
            unsigned int owned=(unsigned int)campaignTables[i],original=campaignSources[i];
            if(PAUSE_WORD(setup+hi[i])!=(0x3c020000|((owned+0x8000)>>16)) ||
               PAUSE_WORD(setup+lo[i])!=(0x24420000|(owned&65535))) continue;
            PAUSE_WORD(setup+hi[i])=0x3c020000|((original+0x8000)>>16);
            PAUSE_WORD(setup+lo[i])=0x24420000|(original&65535);
            PAUSE_WORD(setup+lo[i]+4)=0x24030000|count[i];
            if(i==2 && PAUSE_WORD(setup+0xa4)==(0x3c020000|((owned+0x8000)>>16)))
                PAUSE_WORD(setup+0xa4)=PAUSE_WORD(setup+hi[i]);
            changed=1;
        }
        if(changed && PAUSE_WORD(0x11f0a0)==0x24030064) {
            ((void (*)(int))0x11f0a0)(0);((void (*)(int))0x11f0a0)(2);
        }
    }
    campaignActive=0;campaignOpen=0;pauseOpen=0;
}

/* Measure the same font and scale used by menuBackendText. */
float menuBackendWidth(const char *text)
{
    if(!active || !text) return 0;
    ((void (*)(int))rendererFont(active))(0);
    return ((int (*)(const char *,int,float))(active->print-0x2a0))(text,-1,.7f)/512.f;
}
