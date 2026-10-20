#include "maploader.h"
#ifndef MAP_SEEK
#define MAP_SEEK ((int (*)(int,int,int))0x12ae40)
#endif
const ModuleApi *mapApi;
MapRecord *mapRecords;
int mapCount, mapSelection=-1;

static int validName(const char *text,unsigned int size)
{
    unsigned int i;
    if(!text[0] || text[0]=='.') return 0;
    for(i=0;i<size;++i) {
        unsigned char c=text[i];
        if(!c) return 1;
        if(c<32 || c>126 || c=='/' || c=='\\' || c==':' || c=='"' || c=='*' || c=='?') return 0;
    }
    return 0;
}
static void append(char *dst,unsigned int *pos,const char *src)
{ while(*src) dst[(*pos)++]=*src++; dst[*pos]=0; }
int mapReadExact(int fd,void *dest,unsigned int size)
{
    unsigned char *p=dest;
    while(size) {
        unsigned int chunk=size>0x10000?0x10000:size;
        int n=mapApi->read(fd,p,chunk);
        if(n<=0 || (unsigned int)n>chunk) return 0;
        p+=n; size-=n;
    }
    return 1;
}
int mapOpenPart(const MapRecord *map,const char *extension)
{
    char path[112]; unsigned int pos=0;
    if(!validName(map->filename,sizeof(map->filename))) return -1;
    append(path,&pos,"host0:uya/"); append(path,&pos,map->filename);
#if UYA_MODULE_REGION == 2
    append(path,&pos,".pal");
#endif
    append(path,&pos,extension);
    return mapApi->open(path,1,0);
}
int mapReadPart(const MapRecord *map,const char *extension,void *dest,unsigned int size)
{
    int fd=mapOpenPart(map,extension),ok; unsigned char tail;
    if(fd<0) return 0;
    ok=mapReadExact(fd,dest,size) && mapApi->read(fd,&tail,1)==0;
    mapApi->close(fd);
    return ok;
}
/* MP's resident seek has been checked in both regional SDK images. Campaign
 * catalogs can be read via ModuleApi, but no campaign game-code hooks are used. */
static int sizeMatches(const MapRecord *map,const char *extension,unsigned int size)
{
    int fd,result;
    if(!size) return 1;
    fd=mapOpenPart(map,extension); if(fd<0) return 0;
    result=MAP_SEEK(fd,0,2);
    mapApi->close(fd);
    return result==(int)size;
}
int mapValidate(const MapRecord *map)
{
    if(!mapMultiplayerReady()) return 0;
    return sizeMatches(map,".wad",map->wad) && sizeMatches(map,".world",map->world) &&
           sizeMatches(map,".sound",map->sound) && sizeMatches(map,".map",map->radar);
}
void *memset(void *dest,int value,unsigned int size)
{ unsigned char *p=dest; while(size--) *p++=(unsigned char)value; return dest; }

/* .version metadata is shared by both regions: 48-byte header, author[32],
 * description[256]. Older packs may contain just the header. */
int mapReadInfo(const MapRecord *map,char *author,char *description)
{
    char path[112],header[48]; unsigned int pos=0,i;int fd,ok;
    author[0]=description[0]=0;
    if(!validName(map->filename,sizeof(map->filename))) return 0;
    append(path,&pos,"host0:uya/");append(path,&pos,map->filename);append(path,&pos,".version");
    fd=mapApi->open(path,1,0);if(fd<0) return 0;
    ok=mapReadExact(fd,header,48) && mapReadExact(fd,author,32) && mapReadExact(fd,description,256);
    mapApi->close(fd);
    if(!ok) { author[0]=description[0]=0;return 0; }
    author[31]=0;description[255]=0;
    for(i=0;author[i];++i) if((unsigned char)author[i]<32) author[i]=' ';
    for(i=0;description[i];++i)
        if((unsigned char)description[i]<32 && description[i]!='\n') description[i]=' ';
    return 1;
}

/* Scan state is driven one entry/RPC at a time from Local Play staging. */
static MapRecord *work;
static int phase,candidates,current,kept,waitTicks;
static unsigned int cachedFingerprint;
unsigned int mapCatalogRevision;
#if UYA_MODULE_REGION == 2
#define CACHE "host0:uya/maps.pal.idx"
#else
#define CACHE "host0:uya/maps.ntscu.idx"
#endif
static unsigned int hash(const void *data,unsigned int n)
{const unsigned char *p=data;unsigned int h=2166136261U;while(n--)h=(h^*p++)*16777619U;return h;}
static int compare(const char *a,const char *b)
{while(*a && *a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
static void copyName(char *to,const char *from)
{while((*to++=*from++));}
static int validRecord(const MapRecord *m)
{return validName(m->filename,64) && validName(m->name,32) && m->base>=40 && m->base<=49 &&
    (m->wad||m->world) && m->wad<=0x1000000 && m->world<=0x400000 && m->sound<=0x400000 && m->radar<=0x10800;}
int mapLoadCatalog(void)
{
    unsigned int header[4],i;unsigned char tail;int fd,ok;
    mapCount=0;cachedFingerprint=0;
    fd=mapApi->open(CACHE,1,0);if(fd<0)return 0;
    ok=mapReadExact(fd,header,sizeof(header)) && header[0]==MAP_INDEX_MAGIC && header[1]<=MAP_LIMIT;
    if(ok && header[1]) {mapRecords=mapAlloc(header[1]*sizeof(MapRecord));if(!mapRecords)ok=0;}
    if(ok)ok=mapReadExact(fd,mapRecords,header[1]*sizeof(MapRecord)) && mapApi->read(fd,&tail,1)==0;
    if(mapApi->close(fd)<0)ok=0;
    if(ok)ok=hash(mapRecords,header[1]*sizeof(MapRecord))==header[3];
    if(ok)for(i=0;i<header[1];++i)if(!validRecord(&mapRecords[i])){ok=0;break;}
    if(!ok){mapFree(mapRecords);mapRecords=0;return 0;}
    mapCount=header[1];cachedFingerprint=header[2];++mapCatalogRevision;return 1;
}
static int writeAll(int fd,const void *data,unsigned int size)
{
    const char *p=data;int n;
    while(size){n=mapWrite(fd,p,size);if(n<=0 || (unsigned int)n>size)return 0;p+=n;size-=n;}return 1;
}
static int saveCache(unsigned int fingerprint)
{
    unsigned int header[4]={MAP_INDEX_MAGIC,0,0,0};int fd,ok;
    header[1]=mapCount;header[2]=fingerprint;header[3]=hash(mapRecords,mapCount*sizeof(MapRecord));
    fd=mapApi->open(CACHE,0x602,0666);if(fd<0)return 0;
    ok=writeAll(fd,header,sizeof(header)) && writeAll(fd,mapRecords,mapCount*sizeof(MapRecord));
    if(mapApi->close(fd)<0)ok=0;return ok;
}
static int partSize(const MapRecord *m,const char *ext)
{
    int fd=mapOpenPart(m,ext),size;
    if(fd<0)return 0;
    size=MAP_SEEK(fd,0,2);
    if(mapApi->close(fd)<0)return -1;return size;
}
static int inspect(MapRecord *m)
{
    unsigned char data[384] __attribute__((aligned(64)));char path[112];unsigned int pos=0;int fd,n,total=0,code;
    append(path,&pos,"host0:uya/");append(path,&pos,m->filename);append(path,&pos,".version");
    fd=mapApi->open(path,1,0);if(fd<0)return 0;
    memset(data,0,sizeof(data));
    while(total<(int)sizeof(data)){
        n=mapApi->read(fd,data+total,sizeof(data)-total);
        if(n<0 || n>(int)sizeof(data)-total){mapApi->close(fd);return -1;}
        if(!n)break;total+=n;
    }
    if(mapApi->close(fd)<0)return -1;
    if(total<48 || *(int *)data<0 || *(short *)(data+8))return 0;
    m->version=*(unsigned int *)data;m->base=*(unsigned int *)(data+4);
    for(n=0;n<32 && data[16+n];++n)m->name[n]=data[16+n];
    code=mapOpenPart(m,".code");if(code>=0){mapApi->close(code);return 0;}
    n=partSize(m,".wad");if(n<0)return -1;m->wad=n;
    n=partSize(m,".world");if(n<0)return -1;m->world=n;
    n=partSize(m,".sound");if(n<0)return -1;m->sound=n;
    n=partSize(m,".map");if(n<0)return -1;m->radar=n;
    if(!validRecord(m))return 0;
    return 1;
}
static void finish(int error)
{
    mapFree(work);work=0;phase=0;
    if(error)mapMessage("Map scan failed; retry by re-entering staging");
}
void mapScanStart(void)
{
    if(phase || !mapMultiplayerReady())return;
    if(!mapDirectoryIdle()){mapMessage("Directory service is still busy");return;}
    if(!mapRecords)mapLoadCatalog();
    work=mapAlloc(MAP_LIMIT*sizeof(MapRecord));
    if(!work){mapMessage("Not enough memory to scan custom maps");return;}
    memset(work,0,MAP_LIMIT*sizeof(MapRecord));candidates=current=kept=waitTicks=0;
    ++mapCatalogRevision; /* Re-read descriptions even when the catalog is unchanged. */
    mapDirectoryReset();phase=1;mapApi->print("maploader: Local Play staging scan started\n");
}
int mapScanActive(void){return phase!=0;}
int mapScanCurrent(void){return current;}
int mapScanTotal(void){return phase==4?candidates:0;}
int mapScanTick(void)
{
    int r,n,i;char name[128],selected[64];MapRecord temp;unsigned int fingerprint;
    if(!phase)return 0;
    if(++waitTicks>6000){finish(1);return -1;}
    if(phase==1){
        r=mapDirectoryReady();if(r<0){finish(1);return -1;}
        if(r>0){if(!mapDirectoryRequest(1)){finish(1);return -1;}phase=2;waitTicks=0;}return 0;
    }
    if(phase==2 || phase==3 || phase==5){
        r=mapDirectoryPoll(name);if(r==-999)return 0;waitTicks=0;
        if(phase==5){if(r<0){finish(1);return -1;}phase=4;return 0;}
        if(r<0){mapDirectoryRequest(3);finish(1);return -1;}
        if(phase==3 && !r){if(!mapDirectoryRequest(3)){finish(1);return -1;}phase=5;return 0;}
        if(phase==3){
            for(n=0;name[n];++n);
            if(n>8 && n<72 && !compare(name+n-8,".version")){
                name[n-8]=0;
                if(validName(name,64)){
                    if(candidates==MAP_LIMIT){mapDirectoryRequest(3);finish(1);return -1;}
                    copyName(work[candidates++].filename,name);
                }
            }
        }
        if(!mapDirectoryRequest(2)){finish(1);return -1;}phase=3;return 0;
    }
    if(phase==4 && current<candidates){
        r=inspect(&work[current]);if(r<0){finish(1);return -1;}
        if(r)work[kept++]=work[current];++current;waitTicks=0;return 0;
    }
    /* Stable ordering makes the fingerprint independent of directory order. */
    for(n=1;n<kept;++n){temp=work[n];i=n;while(i && compare(work[i-1].filename,temp.filename)>0){work[i]=work[i-1];--i;}work[i]=temp;}
    fingerprint=hash(work,kept*sizeof(MapRecord));
    if(fingerprint==cachedFingerprint && kept==mapCount){mapMessage("Custom maps are up to date");finish(0);return 1;}
    selected[0]=0;if(mapSelection>=0 && mapSelection<mapCount)copyName(selected,mapRecords[mapSelection].filename);
    /* Sort the displayed list by name after hashing the canonical file order. */
    for(n=1;n<kept;++n){temp=work[n];i=n;while(i && compare(work[i-1].name,temp.name)>0){work[i]=work[i-1];--i;}work[i]=temp;}
    mapFree(mapRecords);mapRecords=work;work=0;mapCount=kept;mapSelection=-1;
    for(i=0;i<mapCount;++i)if(!compare(selected,mapRecords[i].filename))mapSelection=i;
    if(selected[0] && mapSelection<0){mapRestoreBase();mapClearLoad();}
    ++mapCatalogRevision;
    r=saveCache(fingerprint);cachedFingerprint=r?fingerprint:0;
    mapApi->print("maploader: scan complete: %d maps fingerprint=%08x saved=%d\n",mapCount,fingerprint,r);
    mapMessage(r?"Custom maps refreshed":"Maps ready; saving index failed");finish(0);return 1;
}
void mapScanRelease(void)
{
    /* Never free an old multiplayer pointer through the campaign allocator. */
    if(phase && mapApi->mode==UYA_MODE_MULTIPLAYER && mapDirectoryIdle())mapDirectoryRequest(3);
    finish(0);mapFree(mapRecords);mapRecords=0;mapCount=0;mapSelection=-1;cachedFingerprint=0;
}
