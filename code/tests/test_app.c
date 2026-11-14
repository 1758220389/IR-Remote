#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bosch144.h"
#define main firmware_main
#include "../Main/main.c"
#undef main
static unsigned char banks[2][CONFIG_STORE_BANK_SIZE];
static int ir_busy, rx_enabled, ir_send_count, capture_ready;
static UiSignal capture_input, last_sent;
uint32_t SystemCoreClock=72000000u;
void SystemCoreClockUpdate(void) {}
uint32_t SysTick_Config(uint32_t ticks) { return ticks!=72000u; }
void BoardKeys_Init(void) {}
uint8_t BoardKeys_ReadRaw(void) { return 0; }
void OLED_Init(void) {}
void UiView_Render(const UiViewModel *view) { (void)view; }
void IrHardware_Init(void) { ir_busy=0; rx_enabled=0; }
void IrHardware_Poll(uint32_t ms) { (void)ms; }
void IrHardware_SetReceiveEnabled(uint8_t enabled) { rx_enabled=enabled; }
int IrHardware_Busy(void) { return ir_busy; }
int IrHardware_Send(const UiSignal *signal) { if(ir_busy || !signal) return 0; last_sent=*signal; ++ir_send_count; return 1; }
int IrHardware_TryReceive(UiSignal *signal) {
    if(!capture_ready || !rx_enabled) return 0;
    if(capture_ready<0) { capture_ready=0; return -1; }
    *signal=capture_input; capture_ready=0; return 1;
}
static int rd(void *c,unsigned b,uint32_t off,void *out,uint32_t n) {
    (void)c; assert(b<2 && off+n<=CONFIG_STORE_BANK_SIZE); memcpy(out,banks[b]+off,n);return 1;
}
static int erase(void *c,unsigned b) { (void)c;memset(banks[b],255,CONFIG_STORE_BANK_SIZE);return 1; }
static int program(void *c,unsigned b,uint32_t off,uint16_t v) {
    (void)c; banks[b][off]&=(unsigned char)v; banks[b][off+1]&=(unsigned char)(v>>8); return 1;
}
void FlashBackend_Init(ConfigStoreBackend *b) { b->context=0;b->read=rd;b->erase_bank=erase;b->program_halfword=program; }
static void key(uint8_t number,ButtonEventType type) {
    ButtonEvent e;e.key=number;e.type=type;on_button(&ui,&e);
}
int main(void) {
    memset(banks,255,sizeof(banks));
    initialize_application();
    assert(ui.active_profile==0 && !g_app_diagnostics.store_loaded);
    assert(ui.profiles[0].kind==UI_PROFILE_BOSCH_ON && ui.profiles[0].bound_mask==31u);
    { Bosch144State ac;
      key(1,BUTTON_SHORT);assert(Bosch144_StateFromSignal(&ac,&ui.profiles[0].keys[0]));
      assert(Bosch144_GetTempHalf(&ac)==52u && last_sent.length==18u);
      ir_busy=1;key(2,BUTTON_SHORT);ir_busy=0;
      assert(Bosch144_StateFromSignal(&ac,&ui.profiles[0].keys[0]) && Bosch144_GetTempHalf(&ac)==52u);
      key(5,BUTTON_SHORT);assert(ui.profiles[0].kind==UI_PROFILE_BOSCH_OFF && last_sent.length==12u);
      key(5,BUTTON_SHORT);assert(ui.profiles[0].kind==UI_PROFILE_BOSCH_ON && last_sent.length==18u);
      key(3,BUTTON_SHORT);key(4,BUTTON_SHORT);
      assert(Bosch144_StateFromSignal(&ac,&ui.profiles[0].keys[0]));
      assert(Bosch144_GetMode(&ac)==BOSCH144_MODE_DRY && Bosch144_GetFan(&ac)==BOSCH144_FAN_20);
    }
    initialize_application();ir_send_count=0;
    ui.active_profile=2; milliseconds=10; service_application();
    milliseconds=1011; service_application();
    assert(g_app_diagnostics.store_save_ok==1);
    initialize_application();
    assert(ui.active_profile==2 && g_app_diagnostics.store_loaded);
    key(5,BUTTON_LONG);key(5,BUTTON_SHORT);service_application();assert(rx_enabled);
    capture_ready=-1;service_application();
    assert(ui.page==UI_PAGE_RECEIVING && ui.receive_error && !rx_enabled);
    key(5,BUTTON_SHORT);service_application();assert(!ui.receive_error && rx_enabled);
    memset(&capture_input,0,sizeof(capture_input));capture_input.length=7;capture_input.carrier_khz=38;
    {unsigned i;for(i=0;i<7;++i)capture_input.timings[i]=560;}
    capture_ready=1;service_application();assert(ui.page==UI_PAGE_RECEIVED);
    assert(g_app_diagnostics.rx_frames==1 && !rx_enabled);
    key(5,BUTTON_SHORT);assert(ui.learning.bound_mask==16);
    key(5,BUTTON_LONG);key(5,BUTTON_LONG);assert(ui.result==UI_RESULT_SAVED_FLASH);
    assert(ui.result_at==milliseconds);
    initialize_application();assert(ui.profiles[2].bound_mask==16);
    key(5,BUTTON_SHORT);assert(ir_send_count==1);
    ir_busy=1; key(5,BUTTON_SHORT);assert(g_app_diagnostics.tx_rejected==1);
    puts("application integration: capture/bind/save/reboot/replay/selection persistence PASS");return 0;
}
