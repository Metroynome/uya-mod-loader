#include "maploader.h"
const ModuleApi *mapApi;
MapRecord mapRecords[MAP_LIMIT];
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
    result=((int (*)(int,int,int))0x12ae40)(fd,0,2);
    mapApi->close(fd);
    return result==(int)size;
}
int mapValidate(const MapRecord *map)
{
    if(!mapMultiplayerReady()) return 0;
    return sizeMatches(map,".wad",map->wad) && sizeMatches(map,".world",map->world) &&
           sizeMatches(map,".sound",map->sound) && sizeMatches(map,".map",map->radar);
}
int mapLoadCatalog(void)
{
    unsigned int header[2],i; unsigned char tail; int fd;
    mapCount=0; mapSelection=-1;
#if UYA_MODULE_REGION == 2
    fd=mapApi->open("host0:uya/maps.pal.idx",1,0);
#else
    fd=mapApi->open("host0:uya/maps.ntscu.idx",1,0);
#endif
    if(fd<0) return 0;
    if(!mapReadExact(fd,header,sizeof(header)) || header[0]!=MAP_INDEX_MAGIC || header[1]>MAP_LIMIT ||
       !mapReadExact(fd,mapRecords,header[1]*sizeof(MapRecord)) || mapApi->read(fd,&tail,1)!=0) {
        mapApi->close(fd); return 0;
    }
    mapApi->close(fd);
    for(i=0;i<header[1];++i) {
        MapRecord *m=&mapRecords[i];
        if(!validName(m->filename,64) || !validName(m->name,32) ||
           m->base<40 || m->base>49 || (!m->wad && !m->world) ||
           m->wad>0x1000000 || m->world>0x400000 || m->sound>0x400000 ||
           m->radar>0x10800) return 0;
    }
    mapCount=header[1]; return 1;
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
