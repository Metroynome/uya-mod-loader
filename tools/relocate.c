/* Convert EE ELF relocation metadata into the small version-3 module trailer.
 * Usage: relocate linked.elf flat.bin output.bin
 * This is packaging, not a size policy: the runtime owns allocation/validation. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <elf.h>

static void fail(const char *s) { fprintf(stderr,"relocate: %s\n",s); exit(1); }
static unsigned char *readFile(const char *path, size_t *size)
{
    FILE *f=fopen(path,"rb"); unsigned char *p; long n;
    if(!f || fseek(f,0,SEEK_END) || (n=ftell(f))<0 || fseek(f,0,SEEK_SET)) fail("read failed");
    *size=(size_t)n;p=malloc(*size?*size:1);
    if(!p || fread(p,1,*size,f)!=*size || fclose(f)) fail("read failed");
    return p;
}
static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v)
{p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static void bounds(size_t offset,size_t length,size_t size)
{if(offset>size || length>size-offset)fail("truncated ELF/image");}
typedef struct {uint32_t offset,type,value;} Record;
static int order(const void *a,const void *b)
{uint32_t x=((const Record *)a)->offset,y=((const Record *)b)->offset;return x<y?-1:x>y;}
int main(int argc,char **argv)
{
    size_t es,bs;unsigned char *elf,*bin;Elf32_Ehdr *eh;Elf32_Shdr *sh;
    uint32_t base,count=0;unsigned int i,j;FILE *out;Record *records;
    if(argc!=4)fail("usage: relocate ELF FLAT OUTPUT");
    elf=readFile(argv[1],&es);bin=readFile(argv[2],&bs);
    bounds(0,sizeof(*eh),es);eh=(Elf32_Ehdr *)elf;
    if(memcmp(eh->e_ident,ELFMAG,SELFMAG) || eh->e_ident[EI_CLASS]!=ELFCLASS32 ||
       eh->e_ident[EI_DATA]!=ELFDATA2LSB || eh->e_machine!=EM_MIPS ||
       eh->e_shentsize!=sizeof(*sh))fail("expected little-endian MIPS ELF32");
    bounds(eh->e_shoff,(size_t)eh->e_shnum*sizeof(*sh),es);sh=(Elf32_Shdr *)(elf+eh->e_shoff);
    bounds(0,40,bs);base=word(bin+12);
    records=malloc(es/sizeof(Elf32_Rel)*sizeof(*records));if(!records)fail("out of memory");
    out=fopen(argv[3],"wb");if(!out || fwrite(bin,1,bs,out)!=bs)fail("write failed");
    for(i=0;i<eh->e_shnum;++i) {
        Elf32_Rel *rel;Elf32_Sym *sym;unsigned int n,ns;
        if(sh[i].sh_type!=SHT_REL)continue;
        if(sh[i].sh_info>=eh->e_shnum || sh[i].sh_link>=eh->e_shnum)fail("bad section index");
        if(!(sh[sh[i].sh_info].sh_flags&SHF_ALLOC))continue;
        bounds(sh[i].sh_offset,sh[i].sh_size,es);
        bounds(sh[sh[i].sh_link].sh_offset,sh[sh[i].sh_link].sh_size,es);
        rel=(Elf32_Rel *)(elf+sh[i].sh_offset);n=sh[i].sh_size/sizeof(*rel);
        sym=(Elf32_Sym *)(elf+sh[sh[i].sh_link].sh_offset);ns=sh[sh[i].sh_link].sh_size/sizeof(*sym);
        for(j=0;j<n;++j) {
            unsigned int k,type=ELF32_R_TYPE(rel[j].r_info),s=ELF32_R_SYM(rel[j].r_info);
            uint32_t offset=rel[j].r_offset-base,v,target;
            if(!type)continue;
            if(s>=ns)fail("bad relocation symbol");
            if(sym[s].st_shndx==SHN_ABS)continue; /* Game/SDK addresses stay absolute. */
            if(sym[s].st_shndx==SHN_UNDEF || sym[s].st_shndx>=eh->e_shnum)fail("unresolved symbol");
            if(!(sh[sym[s].st_shndx].sh_flags&SHF_ALLOC))fail("nonresident symbol");
            if(type==R_MIPS_PC16)continue; /* Both endpoints move together. */
            bounds((size_t)offset+40,4,bs);v=word(bin+40+offset);
            switch(type) {
            case R_MIPS_32: target=v-base;break;
            case R_MIPS_26: target=((v&0x03ffffffU)<<2)-base;break;
            case R_MIPS_LO16: target=((v&0xffffU)-(base&0xffffU))&0xffffU;break;
            case R_MIPS_HI16:
                /* ELF REL HI16 records pair with the following LO16 for the
                 * same symbol; multiple HIs may share that LO. Include the
                 * signed low half so relocating across 0x8000 carries right. */
                for(k=j+1;k<n;++k)if(ELF32_R_TYPE(rel[k].r_info)==R_MIPS_LO16 && ELF32_R_SYM(rel[k].r_info)==s)break;
                if(k==n)fail("unpaired MIPS HI16 relocation");
                bounds((size_t)(rel[k].r_offset-base)+40,4,bs);
                target=((v&0xffffU)<<16)+(int16_t)word(bin+40+rel[k].r_offset-base)-base;break;
            default: fail("unsupported MIPS relocation (compile with -G0 -mno-abicalls)");
            }
            records[count].offset=offset;records[count].type=type;records[count++].value=target;
        }
    }
    qsort(records,count,sizeof(*records),order);
    for(i=0;i<count;++i) {
        unsigned char record[12];
        if(i && records[i-1].offset==records[i].offset)fail("duplicate relocation");
        put(record,records[i].offset);put(record+4,records[i].type);put(record+8,records[i].value);
        if(fwrite(record,1,12,out)!=12)fail("write failed");
    }
    {unsigned char value[4];put(value,count);if(fseek(out,36,SEEK_SET) || fwrite(value,1,4,out)!=4 || fclose(out))fail("write failed");}
    free(records);free(elf);free(bin);return 0;
}
