/* Host-side binary checks and PNACH writer. No game or Python dependencies. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void fail(const char *message) { fprintf(stderr,"%s\n",message); exit(1); }
static uint32_t number(const char *s) {
    char *end; unsigned long n=strtoul(s,&end,0);
    if(!*s || *end || n>0xffffffffUL) fail("Invalid number");
    return (uint32_t)n;
}
static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void hook(uint32_t site,uint32_t entry) {
    printf("patch=1,EE,E0040000,extended,%08X\n",site-4);
    printf("patch=1,EE,E003DFB0,extended,%08X\n",site-2);
    printf("patch=1,EE,E0020020,extended,%08X\n",site+4);
    printf("patch=1,EE,E00127BD,extended,%08X\n",site+6);
    printf("patch=1,EE,2%07X,extended,%08X\n",site,0x08000000|(entry>>2));
}
int main(int argc,char **argv) {
    FILE *f; long size; unsigned char *data; uint32_t address,bytes,region;
    if(argc<3) fail("Usage: image module FILE REGION ADDRESS [RESERVATION] | pnach FILE CAMPAIGN [MULTIPLAYER]");
    f=fopen(argv[2],"rb"); if(!f) fail("Cannot open image");
    if(fseek(f,0,SEEK_END) || (size=ftell(f))<0 || size>0x1000000 || fseek(f,0,SEEK_SET)) fail("Invalid image size");
    data=calloc((size_t)size+4,1); if(!data) fail("Out of memory");
    if(fread(data,1,(size_t)size,f)!=(size_t)size || fclose(f)) fail("Cannot read image");
    if(!strcmp(argv[1],"module")) {
        if(argc<5 || argc>6 || size<40) fail("Invalid module arguments/header");
        region=number(argv[3]);address=number(argv[4]);bytes=word(data+20);
        if(address>=0xffc00 || bytes>0xffc00-address)
            fail("Module arena is full; reduce the built module set or module size");
        if(word(data)!=0x4d415955 || word(data+4)!=2 || word(data+8)!=region ||
           word(data+12)!=address || address<0xf2f80 || address>=0xffc00 ||
           bytes>0xffc00-address || word(data+16)+40!=(uint32_t)size ||
           word(data+16)>bytes || word(data+24)>=word(data+16) || word(data+28)>=word(data+16) ||
           word(data+32)<1 || word(data+32)>3 || word(data+36)) fail("Invalid module image/layout");
        if(argc==6 && bytes>number(argv[5])) fail("Module outgrew reservation: rebuild and deploy all modules");
        printf("%u\n",bytes);
    } else if(!strcmp(argv[1],"pnach")) {
        long i;
        if(argc<4 || argc>5 || size>0xfd00) fail("Loader overlaps persistent state");
        puts("// UYA Mod Loader: campaign + multiplayer; no heap edits.");
        for(i=0;i<size;i+=4) printf("patch=1,EE,%08X,word,%08X\n",0xa0000+(unsigned)i,word(data+i));
        hook(0x12c994,number(argv[3]));
        if(argc==5) hook(0x139e94,number(argv[4]));
    } else fail("Unknown image command");
    free(data); return ferror(stdout)?1:0;
}
