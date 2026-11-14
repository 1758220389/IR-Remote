#include "config_store.h"
#include <string.h>
#define MAGIC 0x43465249UL
#define COMMIT 0xA55Au
typedef char PayloadMustFit[(CONFIG_STORE_HEADER_SIZE + CONFIG_STORE_PAYLOAD_SIZE <= CONFIG_STORE_BANK_SIZE) ? 1 : -1];
static uint16_t get16(const unsigned char *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const unsigned char *p)
{ return (uint32_t)get16(p) | ((uint32_t)get16(p+2) << 16); }
static void put16(unsigned char *p, uint16_t v)
{ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
static void put32(unsigned char *p, uint32_t v)
{ put16(p,(uint16_t)v); put16(p+2,(uint16_t)(v>>16)); }
static uint32_t crc_byte(uint32_t crc, unsigned char v)
{
    unsigned i;
    crc ^= v;
    for(i=0;i<8;i++) crc=(crc>>1)^((crc&1u)?0xEDB88320UL:0u);
    return crc;
}
static uint32_t header_crc(const unsigned char *h)
{
    uint32_t crc=0xFFFFFFFFUL;
    unsigned i;
    for(i=0;i<24;i++) crc=crc_byte(crc,h[i]);
    crc=crc_byte(crc,h[28]); crc=crc_byte(crc,h[29]);
    return crc;
}
static int ready(const ConfigStore *s)
{ return s && s->backend.read && s->backend.erase_bank && s->backend.program_halfword; }
void ConfigStore_Init(ConfigStore *s, const ConfigStoreBackend *b)
{ if(s) { if(b) s->backend=*b; else memset(s,0,sizeof(*s)); } }
static int read_word(ConfigStore *s, unsigned bank, uint32_t offset, uint16_t *v)
{
    unsigned char b[2];
    if(!s->backend.read(s->backend.context,bank,offset,b,2)) return 0;
    *v=get16(b); return 1;
}
static uint16_t source_word(const UiProfile *profiles, uint32_t offset,
                            unsigned replacement_index, const UiProfile *replacement)
{
    unsigned pi, key, field;
    const UiProfile *p;
    const UiSignal *sig;
    pi=(unsigned)(offset/CONFIG_STORE_PROFILE_SIZE);
    offset%=CONFIG_STORE_PROFILE_SIZE;
    p=(replacement && pi==replacement_index)?replacement:&profiles[pi];
    key=(unsigned)(offset/CONFIG_STORE_SIGNAL_SIZE);
    if(key==UI_KEY_COUNT) return (uint16_t)(p->bound_mask | ((uint16_t)p->kind<<8));
    field=(unsigned)((offset%CONFIG_STORE_SIGNAL_SIZE)/2u);
    sig=&p->keys[key];
    if(field==0) return sig->length;
    if(field==1) return sig->carrier_khz;
    if(field==2) return sig->format;
    return sig->timings[field-3];
}
static void output_word(UiProfile *profiles, uint32_t offset, uint16_t value)
{
    unsigned pi,key,field;
    UiSignal *sig;
    pi=(unsigned)(offset/CONFIG_STORE_PROFILE_SIZE);
    offset%=CONFIG_STORE_PROFILE_SIZE;
    key=(unsigned)(offset/CONFIG_STORE_SIGNAL_SIZE);
    if(key==UI_KEY_COUNT) {
        profiles[pi].bound_mask=(uint8_t)value;
        profiles[pi].kind=(uint8_t)(value>>8); return;
    }
    field=(unsigned)((offset%CONFIG_STORE_SIGNAL_SIZE)/2u);
    sig=&profiles[pi].keys[key];
    if(field==0) sig->length=value;
    else if(field==1) sig->carrier_khz=value;
    else if(field==2) sig->format=value;
    else sig->timings[field-3]=value;
}
/* Only live compact bytes are interpreted; unused slots remain lossless. */
static int signal_valid(uint16_t length, uint16_t carrier, uint16_t format,
                        const uint16_t *timings, int bound)
{
    unsigned j,sum=0;
    if(format>UI_SIGNAL_BOSCH144 || length>UI_SIGNAL_MAX_TIMINGS ||
       (bound && !length)) return 0;
    if(format==UI_SIGNAL_RAW) return 1;
    if((length!=12 && length!=18) || carrier!=38) return 0;
    for(j=0;j<length;j++) if(timings[j]>255u) return 0;
    for(j=0;j<12;j+=2) if((timings[j]^timings[j+1])!=255u) return 0;
    for(j=0;j<6;j++) if(timings[j]!=timings[j+6]) return 0;
    if(length==12) {
        /* Bosch's fixed OFF command, not arbitrary Coolix commands. */
        static const uint16_t off[6]={0xB2,0x4D,0x7B,0x84,0xE0,0x1F};
        for(j=0;j<6;j++) if(timings[j]!=off[j]) return 0;
    } else {
        if(timings[0]!=0xB2u || timings[12]!=0xD5u) return 0;
        for(j=12;j<17;j++) sum+=timings[j];
        if((sum&255u)!=timings[17]) return 0;
    }
    return 1;
}
static int bank_valid(ConfigStore *s, unsigned bank, int committed,
                       uint32_t *generation, uint8_t *active)
{
    unsigned char h[CONFIG_STORE_HEADER_SIZE], bytes[2];
    uint32_t crc,off;
    unsigned i,k,j;
    uint16_t mask,length,kind,format,carrier,timings[18];
    uint32_t signal_offset;
    if(!s->backend.read(s->backend.context,bank,0,h,sizeof(h))) return -1;
    if(get32(h)!=MAGIC || get16(h+4)!=2 || get16(h+6)!=CONFIG_STORE_HEADER_SIZE ||
       get32(h+12)!=CONFIG_STORE_PAYLOAD_SIZE || get16(h+16)!=UI_SIGNAL_MAX_TIMINGS ||
       h[18]!=UI_PROFILE_COUNT || h[19]!=UI_KEY_COUNT ||
       get16(h+22)!=sizeof(UiProfile) || get16(h+28)!=0 ||
       get16(h+30)!=(committed?COMMIT:0xFFFFu)) return 0;
    crc=header_crc(h);
    for(off=0;off<CONFIG_STORE_PAYLOAD_SIZE;off+=2) {
        if(!s->backend.read(s->backend.context,bank,CONFIG_STORE_HEADER_SIZE+off,bytes,2)) return -1;
        crc=crc_byte(crc,bytes[0]); crc=crc_byte(crc,bytes[1]);
    }
    if((crc^0xFFFFFFFFUL)!=get32(h+24) || get16(h+20)>=UI_PROFILE_COUNT) return 0;
    for(i=0;i<UI_PROFILE_COUNT;i++) {
        off=CONFIG_STORE_HEADER_SIZE+i*CONFIG_STORE_PROFILE_SIZE;
        if(!read_word(s,bank,off+UI_KEY_COUNT*CONFIG_STORE_SIGNAL_SIZE,&mask)) return -1;
        kind=(uint16_t)(mask>>8); mask&=255u;
        if((mask & ~((1u<<UI_KEY_COUNT)-1u)) || kind>UI_PROFILE_BOSCH_OFF) return 0;
        if(kind && mask!=((1u<<UI_KEY_COUNT)-1u)) return 0;
        for(k=0;k<UI_KEY_COUNT;k++) {
            signal_offset=off+k*CONFIG_STORE_SIGNAL_SIZE;
            if(!read_word(s,bank,signal_offset,&length) ||
               !read_word(s,bank,signal_offset+2u,&carrier) ||
               !read_word(s,bank,signal_offset+4u,&format)) return -1;
            if(format>UI_SIGNAL_BOSCH144 || length>UI_SIGNAL_MAX_TIMINGS) return 0;
            if(format==UI_SIGNAL_BOSCH144) {
                if(length!=12 && length!=18) return 0;
                for(j=0;j<length;j++)
                    if(!read_word(s,bank,signal_offset+6u+2u*j,&timings[j])) return -1;
            }
            if(!signal_valid(length,carrier,format,timings,(mask & (1u<<k))!=0)) return 0;
            if(kind && k==0 && (length!=18 || format!=UI_SIGNAL_BOSCH144)) return 0;
        }
    }
    *generation=get32(h+8); *active=(uint8_t)get16(h+20);
    return 1;
}
static int newest(ConfigStore *s, uint32_t *generation, uint8_t *active)
{
    uint32_t g0=0,g1=0,delta;
    uint8_t a0=0,a1=0;
    int v0,v1;
    v0=bank_valid(s,0,1,&g0,&a0); v1=bank_valid(s,1,1,&g1,&a1);
    if(v0<0 || v1<0) return -2;
    if(!v0 && !v1) return -1;
    delta=g1-g0;
    if(v1 && (!v0 || (delta!=0 && delta<0x80000000UL))) {
        *generation=g1; *active=a1; return 1;
    }
    *generation=g0; *active=a0; return 0;
}
int ConfigStore_Load(ConfigStore *s, UiProfile *profiles, uint8_t *active)
{
    int bank;
    uint32_t generation,off;
    uint16_t value;
    uint8_t a;
    if(!ready(s) || !profiles || !active) return 0;
    bank=newest(s,&generation,&a);
    if(bank<0) return 0;
    memset(profiles,0,sizeof(UiProfile)*UI_PROFILE_COUNT);
    for(off=0;off<CONFIG_STORE_PAYLOAD_SIZE;off+=2) {
        if(!read_word(s,(unsigned)bank,CONFIG_STORE_HEADER_SIZE+off,&value)) return 0;
        output_word(profiles,off,value);
    }
    *active=a; return 1;
}
static int source_valid(const UiProfile *profiles, uint8_t active,
                         uint8_t index, const UiProfile *replacement)
{
    unsigned i,k;
    const UiProfile *p;
    if(active>=UI_PROFILE_COUNT || (replacement && index>=UI_PROFILE_COUNT)) return 0;
    for(i=0;i<UI_PROFILE_COUNT;i++) {
        p=(replacement && i==index)?replacement:&profiles[i];
        if((p->bound_mask & ~((1u<<UI_KEY_COUNT)-1u)) || p->kind>UI_PROFILE_BOSCH_OFF) return 0;
        if(p->kind && (p->bound_mask!=((1u<<UI_KEY_COUNT)-1u) ||
           p->keys[0].format!=UI_SIGNAL_BOSCH144 || p->keys[0].length!=18)) return 0;
        for(k=0;k<UI_KEY_COUNT;k++) {
            if(!signal_valid(p->keys[k].length,p->keys[k].carrier_khz,
               p->keys[k].format,p->keys[k].timings,(p->bound_mask & (1u<<k))!=0)) return 0;
        }
    }
    return 1;
}
int ConfigStore_SaveReplacement(ConfigStore *s, const UiProfile *profiles,
             uint8_t active, uint8_t replacement_index, const UiProfile *replacement)
{
    unsigned char h[CONFIG_STORE_HEADER_SIZE];
    uint32_t generation=0,crc,off,verify_generation;
    uint16_t value;
    uint8_t old_active,verify_active;
    int bank;
    if(!ready(s) || !profiles ||
       !source_valid(profiles,active,replacement_index,replacement)) return 0;
    bank=newest(s,&generation,&old_active);
    if(bank==-2) return 0;
    bank=bank<0?0:1-bank;
    memset(h,0,sizeof(h));
    put32(h,MAGIC); put16(h+4,2); put16(h+6,CONFIG_STORE_HEADER_SIZE);
    put32(h+8,generation+1u); put32(h+12,CONFIG_STORE_PAYLOAD_SIZE);
    put16(h+16,UI_SIGNAL_MAX_TIMINGS); h[18]=UI_PROFILE_COUNT; h[19]=UI_KEY_COUNT;
    put16(h+20,active); put16(h+22,(uint16_t)sizeof(UiProfile));
    put16(h+30,0xFFFFu);
    crc=header_crc(h);
    for(off=0;off<CONFIG_STORE_PAYLOAD_SIZE;off+=2) {
        value=source_word(profiles,off,replacement_index,replacement);
        crc=crc_byte(crc,(unsigned char)value); crc=crc_byte(crc,(unsigned char)(value>>8));
    }
    put32(h+24,crc^0xFFFFFFFFUL);
    if(!s->backend.erase_bank(s->backend.context,(unsigned)bank)) return 0;
    for(off=0;off<30;off+=2)
        if(!s->backend.program_halfword(s->backend.context,(unsigned)bank,off,get16(h+off))) return 0;
    for(off=0;off<CONFIG_STORE_PAYLOAD_SIZE;off+=2) {
        value=source_word(profiles,off,replacement_index,replacement);
        if(!s->backend.program_halfword(s->backend.context,(unsigned)bank,CONFIG_STORE_HEADER_SIZE+off,value)) return 0;
    }
    if(bank_valid(s,(unsigned)bank,0,&verify_generation,&verify_active)!=1) return 0;
    if(!s->backend.program_halfword(s->backend.context,(unsigned)bank,30,COMMIT)) return 0;
    return bank_valid(s,(unsigned)bank,1,&verify_generation,&verify_active)==1;
}
int ConfigStore_Save(ConfigStore *s,const UiProfile *profiles,uint8_t active)
{ return ConfigStore_SaveReplacement(s,profiles,active,0,0); }
