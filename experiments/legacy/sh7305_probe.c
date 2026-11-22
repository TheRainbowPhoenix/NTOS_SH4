/* SPDX-FileCopyrightText: 2026 Artem Novak
 * SPDX-License-Identifier: BSD-3-Clause
 * SH4AL-DSP BE NTOS probe: P0 backing-address diagnostics and streamed ROM.
 * No gint, SDK I/O, MMU writes or large linked .rodata payload.
 */
#include <stdint.h>

#define ROM_SIZE 0x180000u
#define ROM_PATH "\\fls0\\romprobe.bin"
#define REPORT_PATH "\\fls0\\results.txt"
#define FILE_READ 1u
#define FILE_WRITE 2u
#define FILE_CREATE 4u
#define NOT_MAPPED 0xffffffffu

typedef void (*puts_fn)(const char *);
typedef struct {
    puts_fn puts;                               /* 0 */
    void *reserved_1_2[2];
    void *(*malloc_)(uint32_t);                 /* 3 */
    void (*free_)(void *);                     /* 4 */
    void *reserved_5_10[6];                    /* 1..10 */
    int32_t (*file_open)(const char *,uint32_t);/* 11 */
    int32_t (*file_read)(int32_t,void *,uint32_t);/* 12 */
    int32_t (*file_write)(int32_t,const void *,uint32_t);/* 13 */
    int32_t (*file_close)(int32_t);             /* 14 */
    void *reserved_15_16[2];                    /* 15..16 */
    int32_t (*file_size)(int32_t);              /* 17 */
    int32_t (*file_seek)(int32_t,int32_t,uint32_t); /* 18 */
    int32_t (*file_remove)(const char *);       /* 19 */
    void *reserved_20_27[8];                   /* 20..27 */
    uint32_t (*backing_pa)(uint32_t);           /* 28: NTOS extension */
} ntos_api_t;

typedef struct {
    int opened, size_ok, done, markers[3], error;
    uint32_t bytes, first_bad;
    uint8_t *resident;
} rom_result_t;

static uint8_t chunk[4096];
static char report[2048];
static unsigned used;

static uint32_t reg32(uint32_t address)
{
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static void append(const char *s)
{
    while(*s && used+1u<sizeof(report)) report[used++]=*s++;
    report[used]=0;
}

static void hex(uint32_t x)
{
    static const char digits[]="0123456789ABCDEF";
    int i;
    for(i=28;i>=0;i-=4)
        if(used+1u<sizeof(report)) report[used++]=digits[(x>>i)&15u];
    report[used]=0;
}

static void hex_or_na(uint32_t x)
{
    if(x==NOT_MAPPED)append("N/A");
    else hex(x);
}

static void line_hex(const char *name,uint32_t value)
{
    append(name);append(" ");hex(value);append("\n");
}

static void field(const char *label,uint32_t value)
{
    append(label);append(" ");hex_or_na(value);append("\n");
}

static void sample(const char *title,const uint8_t *ptr)
{
    uint32_t va=NOT_MAPPED,pa=NOT_MAPPED,p1=NOT_MAPPED;
    append("\n");append(title);append("\n");
    if(ptr) {
        va=(uint32_t)(uintptr_t)ptr;
        if(va>=0x80000000u && va<0xc0000000u) {
            pa=va&0x1fffffffu;
            p1=pa|0x80000000u;
        }
    }
    field("VA",va);field("PA",pa);field("P1",p1);
    append("PAGE bytes N/A\n");
    field("VA value",ptr?(uint32_t)*ptr:NOT_MAPPED);
    field("P1 value",p1!=NOT_MAPPED?(uint32_t)*(volatile const uint8_t *)(uintptr_t)p1:NOT_MAPPED);
    append("Alias match: ");
    if(p1==NOT_MAPPED)append("N/A");
    else append(*ptr==*(volatile const uint8_t *)(uintptr_t)p1?"yes":"no");
    append("\n");
}

static rom_result_t verify_rom(const ntos_api_t *api)
{
    rom_result_t result={0};
    int32_t fd,n;
    result.first_bad=0xffffffffu;
    fd=api->file_open(ROM_PATH,FILE_READ);
    if(fd<0) {result.error=fd;return result;}
    result.opened=1;
    n=api->file_size(fd);
    result.size_ok=n==(int32_t)ROM_SIZE;
    if(!result.size_ok) {result.error=n;goto finish;}
    if(api->malloc_) result.resident=(uint8_t *)api->malloc_(ROM_SIZE);
    while(result.bytes<ROM_SIZE) {
        uint32_t count=ROM_SIZE-result.bytes;
        uint32_t i;
        if(count>sizeof(chunk))count=sizeof(chunk);
        n=api->file_read(fd,chunk,count);
        if(n<=0) {result.error=n?n:-1;break;}
        if((uint32_t)n>count){result.error=-2;break;}
        for(i=0;i<(uint32_t)n;++i) {
            uint32_t offset=result.bytes+i;
            uint8_t expected=offset==0u?0x5au:
                offset==0x080000u?0xa5u:
                offset==ROM_SIZE-1u?0x3cu:0u;
            if(chunk[i]!=expected && result.first_bad==NOT_MAPPED)
                result.first_bad=offset;
            if(offset==0u)result.markers[0]=(chunk[i]==0x5au);
            if(offset==0x080000u)result.markers[1]=(chunk[i]==0xa5u);
            if(offset==ROM_SIZE-1u)result.markers[2]=(chunk[i]==0x3cu);
        }
        if(result.resident) {
            for(i=0;i<(uint32_t)n;++i)
                result.resident[result.bytes+i]=chunk[i];
        }
        result.bytes+=(uint32_t)n;
    }
    result.done=result.bytes==ROM_SIZE && result.first_bad==NOT_MAPPED;
    if(!result.done && result.resident) {
        api->free_(result.resident);
        result.resident=0;
    }
finish:
    n=api->file_close(fd);
    if(n<0)result.error=n;
    return result;
}

static int save_report(const ntos_api_t *api)
{
    int32_t fd, n, close_rc;
    uint32_t offset=0;
    /* Hollyhock CREATE opens without truncation. Remove the previous
       report first, after the ROM handle has been closed. */
    (void)api->file_remove(REPORT_PATH);
    fd=api->file_open(REPORT_PATH,FILE_WRITE|FILE_CREATE);
    if(fd<0)return fd;
    while(offset<used) {
        n=api->file_write(fd,report+offset,used-offset);
        if(n<=0) {api->file_close(fd);return n?n:-1;}
        offset+=(uint32_t)n;
    }
    close_rc=api->file_close(fd);
    return close_rc<0?close_rc:0;
}

int main(const ntos_api_t *api,char **argv,char **envp)
{
    uint32_t va,pa,p1,code_word,p1_word,ram_pa;
    int saved, sample_mapped, sample_contiguous;
    rom_result_t rom;
    (void)argv;(void)envp;
    if(!api || !api->puts)return 1;
    used=0;report[0]=0;

    append("SH7305 PROBE RESULTS\n====================\n\n");
    line_hex("PVR  ",reg32(0xff000030u));
    line_hex("PRR  ",reg32(0xff000044u));
    line_hex("CVR  ",reg32(0xff000040u));
    line_hex("CPIDR",reg32(0xff000048u));

    append("\nUSER RAM\n");
    ram_pa=api->backing_pa?api->backing_pa(0x08100000u):NOT_MAPPED;
    append("VA 08100000\n");
    field("PA",ram_pa);
    field("P1",ram_pa==NOT_MAPPED?NOT_MAPPED:ram_pa|0x80000000u);
    append("PAGE bytes ");append(ram_pa==NOT_MAPPED?"N/A":"00001000");append("\n");
    append("RAM span bytes ");append(ram_pa==NOT_MAPPED?"N/A":"00004000");append("\n");

    append("\nSH7305 CLOCKS (raw)\n");
    line_hex("FRQCR ",reg32(0xa4150000u));
    line_hex("PLLCR ",reg32(0xa4150024u));
    line_hex("PLLFRQ",reg32(0xa4150050u));
    append("Frequency Hz: N/A\n");

    append("\nADDIN CODE\n");
    va=(uint32_t)(uintptr_t)&main;
    pa=NOT_MAPPED;
    if(va>=0x80000000u && va<0xc0000000u)pa=va&0x1fffffffu;
    else if(api->backing_pa)pa=api->backing_pa(va);
    p1=pa==NOT_MAPPED?NOT_MAPPED:pa|0x80000000u;
    field("VA",va);field("PA",pa);field("P1",p1);
    append("PAGE bytes ");append(pa==NOT_MAPPED?"N/A":"00001000");append("\n");
    /* The reference uses 16-bit SH instructions, not a 32-bit word. */
    code_word=(uint32_t)*(volatile const uint16_t *)(uintptr_t)va;
    p1_word=p1==NOT_MAPPED?NOT_MAPPED:
        (uint32_t)*(volatile const uint16_t *)(uintptr_t)p1;
    field("VA value",code_word);field("P1 value",p1_word);
    append("Alias match: ");
    append(p1==NOT_MAPPED?"N/A":code_word==p1_word?"yes":"no");
    append("\n");

    rom=verify_rom(api);
    sample("ROM 1.5M START",rom.resident);
    sample("ROM 1.5M MID",rom.resident?rom.resident+0x080000u:0);
    sample("ROM 1.5M END",rom.resident?rom.resident+ROM_SIZE-1u:0);

    sample_mapped=rom.resident!=0;
    if(sample_mapped) {
        uint32_t mem=(uint32_t)(uintptr_t)rom.resident;
        sample_mapped=mem>=0x80000000u && mem<0xc0000000u &&
            ROM_SIZE<=0xc0000000u-mem;
    }
    sample_contiguous=sample_mapped; /* Direct P1/P2 allocation is linear. */
    append("\nLINUX ROM TEST\n");
    line_hex("ROM bytes",ROM_SIZE);
    append("Sample map: ");append(sample_mapped?"YES":"N/A");append("\n");
    append("Samples contiguous: ");append(sample_mapped?
        (sample_contiguous?"YES":"NO"):"N/A");append("\n");
    append("File integrity: ");append(rom.done?"PASS":"FAIL");append("\n");
    append("File first bad: ");hex(rom.first_bad);append("\n");
    append("File read bytes: ");hex(rom.bytes);append("\n");
    append("Note: samples are RAM backing; the file is not mapped ROM.\n");

    saved=save_report(api);
    api->puts("SH7305 PROBE RESULTS written to results.txt");
    api->puts(rom.done?"ROM FILE: PASS":"ROM FILE: FAIL");
    api->puts(saved==0?"SAVE: OK":"SAVE: FAILED");
    if(rom.resident && api->free_)api->free_(rom.resident);
    return saved!=0?2:(rom.done?0:1);
}
