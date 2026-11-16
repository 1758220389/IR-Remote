#include "ac_codec.h"
#include "bosch144.h"
#include "config_store.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char banks[2][CONFIG_STORE_BANK_SIZE];
static int rd(void *c,unsigned bank,uint32_t off,void *p,uint32_t n)
{ (void)c;memcpy(p,banks[bank]+off,n);return 1; }
static int erase(void *c,unsigned bank)
{ (void)c;memset(banks[bank],255,CONFIG_STORE_BANK_SIZE);return 1; }
static int program(void *c,unsigned bank,uint32_t off,uint16_t v)
{ (void)c;banks[bank][off]&=(unsigned char)v;banks[bank][off+1]&=(unsigned char)(v>>8);return 1; }
static void seed(UiSignal *s,unsigned which)
{
    static const uint8_t gree[]={0x19,0x0A,0x60,0x50,2,0x23,0,0xF0};
    static const uint8_t midea[]={0xA1,0x88,0x40,0xFF,0xFF,0x56};
    static const uint8_t coolix[]={0xB2,0x1F,0xC8};
    static const uint8_t haier[]={165,1,32,1,0,192,32,0,167};
    static const uint8_t yrw[]={166,18,0,2,64,32,0,32,0,0,0,0,5,63};
    const uint8_t *bytes=0;
    unsigned i;
    memset(s,0,sizeof(*s));s->carrier_khz=38u;
    switch(which) {
    case 0:s->format=UI_SIGNAL_HAIER;bytes=haier;s->length=sizeof(haier);break;
    case 1:s->format=UI_SIGNAL_MIDEA;bytes=midea;s->length=sizeof(midea);break;
    case 2:s->format=UI_SIGNAL_GREE;bytes=gree;s->length=sizeof(gree);break;
    case 3:s->format=UI_SIGNAL_HAIER_YRW02;bytes=yrw;s->length=sizeof(yrw);break;
    case 4:s->format=UI_SIGNAL_COOLIX;bytes=coolix;s->length=sizeof(coolix);break;
    default: { Bosch144State native;Bosch144_Init(&native);
               assert(Bosch144_Generate(&native,s));return; }
    }
    for(i=0;i<s->length;++i) s->timings[i]=bytes[i];
}
static uint16_t stream(void *context,uint16_t index)
{
    const UiSignal *signal=(const UiSignal *)context;
    if(signal->format==UI_SIGNAL_COOLIX) return Ac_Duration(signal,(uint16_t)(index%100u));
    return Ac_Duration(signal,index);
}
static void confirm_learning(const UiSignal *signal)
{
    UiState ui;
    UiSignal decoded;
    ButtonEvent e;
    uint16_t count=Ac_DurationCount(signal);
    if(!(count&1u)) --count; /* Capture ends at last mark, not TX idle. */
    if(signal->format==UI_SIGNAL_COOLIX) count=299u;
    assert(Ac_Detect(stream,(void *)signal,count,&decoded)==AC_DETECT_MATCH);
    assert(decoded.format==signal->format);
    Ui_Init(&ui,0,0u);e.key=5u;e.type=BUTTON_LONG;Ui_Button(&ui,&e,0u);
    e.type=BUTTON_SHORT;Ui_Button(&ui,&e,0u);
    assert(ui.page==UI_PAGE_DEVICE_TYPE);
    Ui_Button(&ui,&e,0u);assert(Ui_Receive(&ui,&decoded,0u));
    assert(ui.page==UI_PAGE_AC_CONFIRM && !ui.profiles[0].kind);
    Ui_Button(&ui,&e,0u);assert(ui.page==UI_PAGE_SAVE_SELECT);
    assert(Ac_ProfileValid(&ui.learning));
}
int main(void)
{
    UiSignal signal,tx;
    UiProfile profiles[3],restored[3];
    AcState state;
    AcStatus status, off_status;
    ConfigStore store;
    ConfigStoreBackend backend;
    unsigned which,key;
    uint8_t active;
    backend.context=0;backend.read=rd;backend.erase_bank=erase;backend.program_halfword=program;
    ConfigStore_Init(&store,&backend);
    for(which=0;which<6u;++which) {
        memset(profiles,0,sizeof(profiles));memset(banks,255,sizeof(banks));
        seed(&signal,which);assert(Ac_Validate(&signal));
        confirm_learning(&signal);
        assert(Ac_ProfileFromSignal(&profiles[0],&signal));
        for(key=1u;key<=5u;++key) {
            assert(Ac_ProfileLoad(&state,&profiles[0]));
            assert(Ac_Button(&state,(uint8_t)key));
            assert(Ac_Encode(&state,&tx) && Ac_Validate(&tx));
            Ac_ProfileCommit(&profiles[0],&state);
            assert(Ac_ProfileValid(&profiles[0]));
            assert(Ac_ProfileLoad(&state,&profiles[0]));
            assert(Ac_GetStatus(&state,&status));
            assert(ConfigStore_Save(&store,profiles,0));
            assert(ConfigStore_Load(&store,restored,&active));
            assert(active==0 && !memcmp(profiles,restored,sizeof(profiles)));
        }
        assert(!state.power);
        for(key=1u;key<=4u;++key) {
            assert(Ac_ProfileLoad(&state,&profiles[0]) && !state.power);
            assert(Ac_Button(&state,(uint8_t)key) && !state.power);
            assert(Ac_Encode(&state,&tx) && Ac_Validate(&tx));
            Ac_ProfileCommit(&profiles[0],&state);
            assert(Ac_ProfileValid(&profiles[0]));
            assert(ConfigStore_Save(&store,profiles,1u));
            assert(ConfigStore_Load(&store,restored,&active) && active==1u);
            profiles[0]=restored[0];
            assert(Ac_ProfileLoad(&state,&profiles[0]) && !state.power);
        }
        assert(Ac_GetStatus(&state,&off_status));
        assert(Ac_Button(&state,5u));Ac_ProfileCommit(&profiles[0],&state);
        assert(Ac_ProfileLoad(&state,&profiles[0]) && state.power);
        assert(Ac_GetStatus(&state,&status));
        assert(status.temp_half==off_status.temp_half && status.mode==off_status.mode &&
               status.fan==off_status.fan);
        printf("AC profile/control/Flash roundtrip: %s PASS\n",Ac_Name(signal.format));
    }
    return 0;
}
