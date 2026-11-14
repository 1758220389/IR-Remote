/* From repository root (output paths should point to your scratch folder):
 * gcc -std=c90 -pedantic -Wall -Wextra -Werror -Icode/Main
 *     code/Main/config_store.c code/tests/test_config_store.c -o test.exe
 * Optional Windows mapped-address adapter test additionally compiles
 * code/Main/flash_backend.c with -DCONFIG_STORE_TEST_ADAPTER -DSTM32F10X_MD
 * -Dinline=__inline__ -Icode/Library -Icode/Start. The inline define is only
 * for legacy vendor CMSIS headers, not the C90 production modules.
 * Large flash/source/output fixtures exist ONLY in this native test file.
 */
#include "config_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char flash[2][CONFIG_STORE_BANK_SIZE];
static UiProfile source[UI_PROFILE_COUNT], output[UI_PROFILE_COUNT];
static unsigned erases[2];
static int fail_after = -1, writes;
static int read_failure, erase_failure, partial_write, corrupt_write;
static int rd(void *c, unsigned b, uint32_t o, void *d, uint32_t n)
{ (void)c; if(read_failure || b>1 || o+n>CONFIG_STORE_BANK_SIZE) return 0; memcpy(d,flash[b]+o,n); return 1; }
static int erase(void *c,unsigned b)
{ (void)c; assert(b<2); ++erases[b]; if(erase_failure) return 0; memset(flash[b],255,sizeof(flash[b])); return 1; }
static int prog(void *c,unsigned b,uint32_t o,uint16_t v)
{
    unsigned i; (void)c; assert(b<2 && !(o&1) && o+2<=CONFIG_STORE_BANK_SIZE);
    if(fail_after>=0 && writes++==fail_after) {
        if(partial_write) flash[b][o]&=(unsigned char)v;
        return 0;
    }
    for(i=0;i<2;i++) { unsigned char x=(unsigned char)(v>>(8*i));
        assert((flash[b][o+i]&x)==x); flash[b][o+i]&=x; }
    if(corrupt_write && o==CONFIG_STORE_HEADER_SIZE+8) flash[b][o]^=1;
    return 1;
}
static ConfigStore store;
static void setup(void)
{
    ConfigStoreBackend b;
    memset(flash,255,sizeof(flash)); memset(source,0,sizeof(source));
    memset(output,0,sizeof(output)); memset(erases,0,sizeof(erases));
    fail_after=-1; writes=0;
    read_failure=erase_failure=partial_write=corrupt_write=0;
    b.context=0; b.read=rd; b.erase_bank=erase; b.program_halfword=prog;
    ConfigStore_Init(&store,&b);
}
static void empty_test(void)
{
    uint8_t active=99;
    setup(); memset(output,0x5a,sizeof(output));
    assert(!ConfigStore_Load(&store,output,&active));
    assert(active==99 && ((unsigned char *)output)[0]==0x5a);
}
static void roundtrip_test(void)
{
    uint8_t active=99;
    setup(); source[2].bound_mask=1;
    source[2].keys[0].length=UI_SIGNAL_MAX_TIMINGS;
    source[2].keys[0].carrier_khz=38;
    source[2].keys[0].timings[UI_SIGNAL_MAX_TIMINGS-1]=65535u;
    assert(ConfigStore_Save(&store,source,2));
    assert(ConfigStore_Load(&store,output,&active));
    assert(active==2 && memcmp(source,output,sizeof(source))==0);
    assert(erases[0]==1 && erases[1]==0);
    assert(flash[0][30]==0x5a && flash[0][31]==0xa5);
    assert(flash[0][4]==2 && flash[0][5]==0);
    assert(CONFIG_STORE_SIGNAL_SIZE==518u);
    assert(CONFIG_STORE_HEADER_SIZE+CONFIG_STORE_PAYLOAD_SIZE==7808u);
    assert(ConfigStore_Save(&store,source,1));
    assert(ConfigStore_Load(&store,output,&active) && active==1);
    assert(erases[0]==1 && erases[1]==1);
}
static void bosch_profile(UiProfile *p, uint8_t kind)
{
    static const uint16_t frame[18]={178,77,63,192,0,255,
                                    178,77,63,192,0,255,
                                    213,100,0,16,0,73};
    unsigned k;
    memset(p,0,sizeof(*p)); p->bound_mask=31; p->kind=kind;
    for(k=0;k<UI_KEY_COUNT;k++) {
        p->keys[k].format=UI_SIGNAL_BOSCH144;
        p->keys[k].carrier_khz=38; p->keys[k].length=18;
        memcpy(p->keys[k].timings,frame,sizeof(frame));
        p->keys[k].timings[255]=65535u; /* Preserve unused slots, too. */
    }
}
static void compact_roundtrip_test(void)
{
    uint8_t active;
    setup(); bosch_profile(&source[0],UI_PROFILE_BOSCH_ON);
    bosch_profile(&source[1],UI_PROFILE_BOSCH_OFF);
    bosch_profile(&source[2],UI_PROFILE_RAW);
    source[2].keys[1].format=UI_SIGNAL_RAW;
    source[2].keys[1].length=256; source[2].keys[1].timings[0]=9000;
    assert(ConfigStore_Save(&store,source,2));
    assert(ConfigStore_Load(&store,output,&active));
    assert(active==2 && !memcmp(source,output,sizeof(source)));
    assert(flash[0][36]==UI_SIGNAL_BOSCH144 && flash[0][37]==0);
    assert(flash[0][33+UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE]==UI_PROFILE_BOSCH_ON);
    source[1].kind=UI_PROFILE_BOSCH_ON;
    assert(ConfigStore_SaveReplacement(&store,source,1,1,&output[1]));
    assert(ConfigStore_Load(&store,output,&active));
    assert(output[1].kind==UI_PROFILE_BOSCH_OFF);
}
static void reject_save_test(void)
{
    UiProfile replacement;
    setup();
    assert(!ConfigStore_Save(&store,source,UI_PROFILE_COUNT));
    source[0].bound_mask=0x80;
    assert(!ConfigStore_Save(&store,source,0));
    source[0].bound_mask=1;
    assert(!ConfigStore_Save(&store,source,0));
    source[0].bound_mask=0; source[1].keys[2].length=UI_SIGNAL_MAX_TIMINGS+1;
    assert(!ConfigStore_Save(&store,source,0));
    memset(source,0,sizeof(source)); memset(&replacement,0,sizeof(replacement));
    assert(!ConfigStore_SaveReplacement(&store,source,0,3,&replacement));
    replacement.bound_mask=0x80;
    assert(!ConfigStore_SaveReplacement(&store,source,0,1,&replacement));
    assert(erases[0]==0 && erases[1]==0);
}
static uint32_t test_crc_byte(uint32_t c, unsigned char b)
{
    unsigned i; c^=b;
    for(i=0;i<8;i++) c=(c>>1)^((c&1)?0xedb88320UL:0);
    return c;
}
static void wire32(unsigned b,unsigned o,uint32_t v)
{ unsigned i; for(i=0;i<4;i++) flash[b][o+i]=(unsigned char)(v>>(i*8)); }
static void repair_crc(unsigned bank)
{
    uint32_t c=0xffffffffUL; unsigned i;
    for(i=0;i<24;i++) c=test_crc_byte(c,flash[bank][i]);
    c=test_crc_byte(c,flash[bank][28]); c=test_crc_byte(c,flash[bank][29]);
    for(i=0;i<CONFIG_STORE_PAYLOAD_SIZE;i++) c=test_crc_byte(c,flash[bank][32+i]);
    wire32(bank,24,c^0xffffffffUL);
}
static void compact_header_test(void)
{
    unsigned i,j;
    uint8_t active;
    for(i=0;i<2;i++) {
        setup(); bosch_profile(&source[0],UI_PROFILE_BOSCH_ON);
        assert(ConfigStore_Save(&store,source,0));
        if(i==0) {
            source[0].keys[0].timings[0]=177;
            source[0].keys[0].timings[1]=78;
            source[0].keys[0].timings[6]=177;
            source[0].keys[0].timings[7]=78;
        } else {
            source[0].keys[0].timings[12]=212;
            source[0].keys[0].timings[17]=72; /* Still a valid sum. */
        }
        assert(!ConfigStore_Save(&store,source,0));
        assert(erases[0]==1 && erases[1]==0);
        for(j=0;j<18;j++) flash[0][32+6+j*2]=(unsigned char)source[0].keys[0].timings[j];
        repair_crc(0); memset(output,0x5a,sizeof(output)); active=99;
        assert(!ConfigStore_Load(&store,output,&active));
        assert(active==99 && ((unsigned char *)output)[0]==0x5a);
    }
}
static void compact_off_test(void)
{
    static const uint16_t off[12]={178,77,123,132,224,31,178,77,123,132,224,31};
    uint8_t active;
    unsigned i;
    setup(); source[0].bound_mask=1;
    source[0].keys[0].format=UI_SIGNAL_BOSCH144;
    source[0].keys[0].carrier_khz=38; source[0].keys[0].length=12;
    memcpy(source[0].keys[0].timings,off,sizeof(off));
    assert(ConfigStore_Save(&store,source,1));
    assert(ConfigStore_Load(&store,output,&active));
    assert(active==1 && !memcmp(source,output,sizeof(source)));
    /* Matching/complemented but unknown 96-bit command is not Bosch OFF. */
    for(i=0;i<2;i++) {
        source[0].keys[0].timings[i*6+2]=122;
        source[0].keys[0].timings[i*6+3]=133;
        flash[0][32+6+(i*6+2)*2]=122;
        flash[0][32+6+(i*6+3)*2]=133;
    }
    assert(!ConfigStore_Save(&store,source,1));
    assert(erases[0]==1 && erases[1]==0);
    repair_crc(0); memset(output,0x5a,sizeof(output)); active=99;
    assert(!ConfigStore_Load(&store,output,&active));
    assert(active==99 && ((unsigned char *)output)[0]==0x5a);
}
static void invalid_compact(unsigned which, unsigned *offset, uint16_t *value)
{
    UiSignal *sig=&source[0].keys[0];
    *offset=0;
    switch(which) {
    case 0: *offset=4; *value=2; sig->format=*value; break;
    case 1: *offset=4; *value=256; sig->format=*value; break;
    case 2: *value=17; sig->length=*value; break;
    case 3: *offset=2; *value=37; sig->carrier_khz=*value; break;
    case 4: *offset=6; *value=256; sig->timings[0]=*value; break;
    case 5: *offset=8; *value=0; sig->timings[1]=*value; break;
    case 6: *offset=18; *value=179; sig->timings[6]=*value; break;
    case 7: *offset=40; *value=72; sig->timings[17]=*value; break;
    case 8: *offset=UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE;
            *value=0x031f; source[0].kind=3; break;
    case 9: *offset=UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE;
            *value=0x011e; source[0].bound_mask=30; break;
    case 10: *offset=4; *value=UI_SIGNAL_RAW; sig->format=*value; break;
    default: *value=12; sig->length=*value; break;
    }
}
static void compact_rejection_test(void)
{
    unsigned i,off;
    uint16_t value;
    uint8_t active;
    for(i=0;i<12;i++) {
        setup(); bosch_profile(&source[0],i<8?UI_PROFILE_RAW:UI_PROFILE_BOSCH_ON);
        invalid_compact(i,&off,&value);
        assert(!ConfigStore_Save(&store,source,0));
        assert(erases[0]==0 && erases[1]==0);
        setup(); bosch_profile(&source[0],i<8?UI_PROFILE_RAW:UI_PROFILE_BOSCH_ON);
        assert(ConfigStore_Save(&store,source,0));
        invalid_compact(i,&off,&value);
        flash[0][32+off]=(unsigned char)value;
        flash[0][33+off]=(unsigned char)(value>>8); repair_crc(0);
        memset(output,0x5a,sizeof(output)); active=99;
        assert(!ConfigStore_Load(&store,output,&active));
        assert(active==99 && ((unsigned char *)output)[0]==0x5a);
        assert(((unsigned char *)output)[sizeof(output)-1]==0x5a);
    }
}
static void reject_load_test(void)
{
    unsigned i;
    uint8_t active;
    for(i=0;i<12;i++) {
        setup(); assert(ConfigStore_Save(&store,source,0));
        if(i==0) flash[0][20]=UI_PROFILE_COUNT;
        if(i==1) flash[0][32+UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE]=0x80;
        if(i==2) { flash[0][32]=(unsigned char)(UI_SIGNAL_MAX_TIMINGS+1);
                    flash[0][33]=(unsigned char)((UI_SIGNAL_MAX_TIMINGS+1)>>8); }
        if(i==3) flash[0][32+UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE]=1;
        if(i==4) flash[0][33+UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE]=1;
        if(i==5) flash[0][21]=1; /* Reject high byte, not truncated active. */
        if(i==6) flash[0][12]^=1;
        if(i==7) flash[0][4]=1; /* Schema 1 is explicitly unsupported. */
        if(i==8) flash[0][6]^=1;
        if(i==9) flash[0][30]^=1;
        if(i==10) flash[0][16]^=1;
        if(i==11) flash[0][22]^=1;
        repair_crc(0); active=99; memset(output,0x5a,sizeof(output));
        assert(!ConfigStore_Load(&store,output,&active));
        assert(active==99 && ((unsigned char *)output)[0]==0x5a);
    }
}
static unsigned char preserved[CONFIG_STORE_BANK_SIZE];
static void atomic_test(void)
{
    static const int points[]={0,4,14,15,50,1000,(15+CONFIG_STORE_PAYLOAD_SIZE/2)};
    unsigned i;
    uint8_t active;
    for(i=0;i<sizeof(points)/sizeof(points[0]);i++) {
        setup(); assert(ConfigStore_Save(&store,source,1));
        memcpy(preserved,flash[0],sizeof(preserved));
        source[0].keys[0].carrier_khz=38;
        writes=0; fail_after=points[i]; partial_write=1;
        assert(!ConfigStore_Save(&store,source,2));
        assert(!memcmp(preserved,flash[0],sizeof(preserved)));
        assert(ConfigStore_Load(&store,output,&active) && active==1);
        assert(output[0].keys[0].carrier_khz==0);
    }
    setup(); assert(ConfigStore_Save(&store,source,1));
    assert(ConfigStore_Save(&store,source,2));
    flash[1][CONFIG_STORE_HEADER_SIZE+10]^=1;
    assert(ConfigStore_Load(&store,output,&active) && active==1);
    flash[0][24]^=1;
    assert(!ConfigStore_Load(&store,output,&active));
}
static void compact_atomic_test(void)
{
    static const int points[]={0,14,15,17,18,20,23,
        15+UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE/2,
        15+CONFIG_STORE_PAYLOAD_SIZE/2};
    unsigned i;
    uint8_t active;
    for(i=0;i<sizeof(points)/sizeof(points[0]);i++) {
        setup(); bosch_profile(&source[0],UI_PROFILE_BOSCH_ON);
        assert(ConfigStore_Save(&store,source,1));
        memcpy(preserved,flash[0],sizeof(preserved));
        source[0].kind=UI_PROFILE_BOSCH_OFF;
        writes=0; fail_after=points[i]; partial_write=1;
        assert(!ConfigStore_Save(&store,source,2));
        assert(!memcmp(preserved,flash[0],sizeof(preserved)));
        assert(ConfigStore_Load(&store,output,&active) && active==1);
        assert(output[0].kind==UI_PROFILE_BOSCH_ON);
        assert(output[0].keys[0].format==UI_SIGNAL_BOSCH144);
    }
    setup(); bosch_profile(&source[0],UI_PROFILE_BOSCH_ON);
    assert(ConfigStore_Save(&store,source,1));
    source[0].kind=UI_PROFILE_BOSCH_OFF;
    assert(ConfigStore_Save(&store,source,2));
    flash[1][36]=2; repair_crc(1); /* Invalid semantic record despite valid CRC. */
    assert(ConfigStore_Load(&store,output,&active) && active==1);
    assert(output[0].kind==UI_PROFILE_BOSCH_ON);
}
static void replacement_rollover_test(void)
{
    UiProfile replacement;
    uint8_t active;
    setup(); memset(&replacement,0,sizeof(replacement));
    replacement.bound_mask=2; replacement.keys[1].length=2;
    replacement.keys[1].carrier_khz=38; replacement.keys[1].timings[0]=9000;
    source[1].bound_mask=1; source[1].keys[0].length=1;
    assert(ConfigStore_SaveReplacement(&store,source,2,1,&replacement));
    assert(source[1].bound_mask==1);
    assert(ConfigStore_Load(&store,output,&active) && active==2);
    assert(output[1].bound_mask==2 && output[1].keys[0].length==0);
    assert(output[1].keys[1].timings[0]==9000);
    wire32(0,8,0xffffffffUL); repair_crc(0);
    assert(ConfigStore_Save(&store,source,1));
    assert(flash[1][8]==0 && flash[1][9]==0 && flash[1][10]==0 && flash[1][11]==0);
    assert(ConfigStore_Load(&store,output,&active) && active==1);
    assert(ConfigStore_Save(&store,source,0));
    assert(ConfigStore_Load(&store,output,&active) && active==0);
}
#ifdef CONFIG_STORE_TEST_ADAPTER
#include "flash_backend.h"
#include "stm32f10x_flash.h"
/* Windows native test maps the real target addresses, no production hooks. */
__declspec(dllimport) void * __stdcall VirtualAlloc(void *, size_t, unsigned long, unsigned long);
static unsigned page_calls, program_calls, locked=1;
static int hw_error, hw_lie;
void FLASH_Unlock(void) { locked=0; }
void FLASH_Lock(void) { locked=1; }
void FLASH_ClearFlag(uint32_t f) { (void)f; assert(!locked); }
FLASH_Status FLASH_ErasePage(uint32_t address)
{
    assert(!locked && address>=FLASH_BACKEND_BANK0_ADDRESS && address<0x08010000UL);
    assert(address%1024==0); ++page_calls;
    if(hw_error) return FLASH_ERROR_PG;
    if(!hw_lie) memset((void *)(uintptr_t)address,255,1024);
    return FLASH_COMPLETE;
}
FLASH_Status FLASH_ProgramHalfWord(uint32_t address,uint16_t value)
{
    volatile uint16_t *p=(volatile uint16_t *)(uintptr_t)address;
    assert(!locked && address>=FLASH_BACKEND_BANK0_ADDRESS && address<0x08010000UL);
    ++program_calls;
    if(hw_error) return FLASH_ERROR_PG;
    if(!hw_lie) *p &= value;
    return FLASH_COMPLETE;
}
static void adapter_test(void)
{
    ConfigStoreBackend b;
    ConfigStore mapped;
    uint8_t active;
    unsigned before;
    unsigned char bytes[2];
    assert(VirtualAlloc((void *)(uintptr_t)0x08000000UL,65536,0x3000,0x04)==(void *)(uintptr_t)0x08000000UL);
    FlashBackend_Init(&b);
    assert(b.context==0);
    memset((void *)(uintptr_t)0x08000000UL,0,65536);
    assert(b.erase_bank(b.context,0)); assert(page_calls==8 && locked);
    assert(*(volatile unsigned char *)(uintptr_t)0x0800BFFFUL==0);
    assert(*(volatile unsigned char *)(uintptr_t)0x0800E000UL==0);
    assert(b.program_halfword(b.context,0,0,0x1234)); assert(locked);
    assert(b.read(b.context,0,0,bytes,2)); assert(bytes[0]==0x34 && bytes[1]==0x12);
    assert(!b.program_halfword(b.context,0,0,0xFFFF));
    assert(!b.program_halfword(b.context,0,1,0));
    assert(!b.program_halfword(b.context,2,0,0));
    assert(!b.read(b.context,0,8191,bytes,2));
    assert(!b.read(b.context,0,0xffffffffUL,bytes,2));
    assert(!b.erase_bank(b.context,2));
    hw_error=1;
    assert(!b.erase_bank(b.context,1) && locked);
    assert(!b.program_halfword(b.context,0,2,0) && locked);
    hw_error=0; hw_lie=1;
    assert(!b.erase_bank(b.context,1) && locked);
    assert(!b.program_halfword(b.context,0,2,0) && locked);
    hw_lie=0;
    assert(b.erase_bank(b.context,1) && locked);
    assert(program_calls==3);
    ConfigStore_Init(&mapped,&b); memset(source,0,sizeof(source));
    source[0].bound_mask=1; source[0].keys[0].length=UI_SIGNAL_MAX_TIMINGS;
    source[0].keys[0].carrier_khz=38;
    source[0].keys[0].timings[UI_SIGNAL_MAX_TIMINGS-1]=65535u;
    before=page_calls; assert(ConfigStore_Save(&mapped,source,2));
    assert(page_calls-before==8 && locked);
    assert(ConfigStore_Load(&mapped,output,&active) && active==2);
    assert(!memcmp(source,output,sizeof(source)));
    before=page_calls; assert(ConfigStore_Save(&mapped,source,1));
    assert(page_calls-before==8 && locked);
    assert(ConfigStore_Load(&mapped,output,&active) && active==1);
    puts("flash_backend: mapped STM32 addresses/8-page bounds/status/readback/end-to-end PASS");
}
#endif
static void backend_failure_test(void)
{
    uint8_t active;
    setup(); assert(ConfigStore_Save(&store,source,1));
    memcpy(preserved,flash[0],sizeof(preserved));
    read_failure=1;
    assert(!ConfigStore_Save(&store,source,2));
    assert(erases[0]==1 && erases[1]==0);
    read_failure=0; erase_failure=1;
    assert(!ConfigStore_Save(&store,source,2));
    erase_failure=0; corrupt_write=1;
    assert(!ConfigStore_Save(&store,source,2));
    assert(flash[1][30]==255 && flash[1][31]==255);
    assert(!memcmp(preserved,flash[0],sizeof(preserved)));
    assert(ConfigStore_Load(&store,output,&active) && active==1);
}
int main(void)
{
    empty_test(); roundtrip_test(); compact_roundtrip_test(); reject_save_test(); reject_load_test();
    compact_header_test(); compact_off_test(); compact_rejection_test();
    atomic_test(); compact_atomic_test(); replacement_rollover_test(); backend_failure_test();
#ifdef CONFIG_STORE_TEST_ADAPTER
    adapter_test();
#endif
    puts("config_store: 12 groups PASS"); return 0;
}
