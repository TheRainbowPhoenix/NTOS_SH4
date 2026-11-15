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
    void *reserved_1_10[10];                    /* 1..10 */
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
    append("0x");
    for(i=28;i>=0;i-=4) {
        if(used+1u<sizeof(report))
            report[used++]=digits[(x>>i)&15u];
    }
    report[used]=0;
}

static void decimal(uint32_t value)
{
    char s[12];
    unsigned n=0;
    do {s[n++]=(char)('0'+value%10u);value/=10u;} while(value && n<sizeof(s));
    while(n && used+1u<sizeof(report)) report[used++]=s[--n];
    report[used]=0;
}

static void line_hex(const char *name,uint32_t value)
{
    append(name);append("=");hex(value);append("\n");
}

static void line_dec(const char *name,uint32_t value)
{
    append(name);append("=");decimal(value);append("\n");
}

static void line_bool(const char *name,int value)
{
    append(name);append("=");append(value?"YES":"NO");append("\n");
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
        result.bytes+=(uint32_t)n;
    }
    result.done=result.bytes==ROM_SIZE && result.first_bad==NOT_MAPPED;
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
    uint32_t mmucr, va, pa;
    int code_match=0, saved;
    rom_result_t rom;
    (void)argv;(void)envp;
    if(!api || !api->puts)return 1;

    used=0;report[0]=0;
    append("SH7305 PROBE / NTOS SH4AL-DSP BE\n");
    append("==============================\n");
    line_hex("PVR",reg32(0xff000030u));
    line_hex("PRR",reg32(0xff000044u));
    line_hex("CVR",reg32(0xff000040u));
    line_hex("CPID",reg32(0xff000048u));
    mmucr=reg32(0xff000010u);
    line_hex("MMUCR",mmucr);
    line_bool("AT",!!(mmucr&1u));
    line_dec("UTLB_URC",(mmucr>>10)&63u);
    line_dec("UTLB_URB",(mmucr>>18)&63u);

    append("\nCODE\n");
    va=(uint32_t)(uintptr_t)&main;
    line_hex("CODE_VA",va);
    pa=NOT_MAPPED;
    if(va>=0x80000000u && va<0xc0000000u)
        pa=va&0x1fffffffu;
    else if(api->backing_pa)
        pa=api->backing_pa(va);
    line_hex("CODE_BACKING_PA",pa);
    if(pa!=NOT_MAPPED) {
        uint32_t a=reg32(va);
        uint32_t b=reg32(pa|0x80000000u);
        line_hex("CODE_VA_WORD",a);
        line_hex("CODE_P1_WORD",b);
        code_match=(a==b);
    }
    line_bool("CODE_MATCH",code_match);
    append("CODE_NOTE=physical backing, not proof of current UTLB residency\n");

    append("\nROM FILE CHECK\n");
    append("ROM_PATH=" ROM_PATH "\n");
    line_dec("ROM_EXPECTED_SIZE",ROM_SIZE);
    rom=verify_rom(api);
    line_bool("ROM_OPEN",rom.opened);
    line_bool("ROM_LENGTH_OK",rom.size_ok);
    line_dec("ROM_BYTES_READ",rom.bytes);
    line_bool("ROM_START_5A",rom.markers[0]);
    line_bool("ROM_MID_A5",rom.markers[1]);
    line_bool("ROM_END_3C",rom.markers[2]);
    line_hex("ROM_FIRST_BAD",rom.first_bad);
    line_bool("ROM_DATA_PASS",rom.done);
    line_hex("ROM_IO_ERROR",(uint32_t)rom.error);
    append("ROM_NOTE=streamed file verification, NOT 1.5MiB contiguous ROM mapping\n");

    saved=save_report(api);
    api->puts("SH7305 probe finished; P0 backing address resolved:");
    api->puts(pa!=NOT_MAPPED?"YES":"NO");
    api->puts(rom.done?"ROM FILE: PASS":"ROM FILE: FAIL");
    api->puts(saved==0?"RESULTS: \\fls0\\results.txt":"RESULTS: WRITE FAILED");
    return saved!=0?2:(rom.done?0:1);
}
