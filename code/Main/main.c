#include "stm32f10x.h"
#include "OLED.h"
#include "app.h"
#include "board_keys.h"
#include "ui_presenter.h"
#include "ir_hardware.h"
#include "config_store.h"
#include "flash_backend.h"
#include "bosch144.h"
#include <string.h>

static volatile uint32_t milliseconds;
static UiState ui;
static ButtonsState buttons;
static UiPresentation presentation;
static ConfigStore store;
static uint8_t saved_active, observed_active, model_dirty;
static uint32_t selection_changed_at, last_service_ms;
volatile AppDiagnostics g_app_diagnostics;

void App_Tick1ms(void) { ++milliseconds; }
int App_ReceiveSignal(const UiSignal *signal)
{
    if (!Ui_Receive(&ui,signal,milliseconds)) return 0;
    ++g_app_diagnostics.rx_frames;
    g_app_diagnostics.last_rx_length=signal->length;
    return 1;
}
void App_SetBackend(const UiCallbacks *callbacks)
{
    if (callbacks!=0) ui.callbacks=*callbacks;
    else memset(&ui.callbacks,0,sizeof(ui.callbacks));
}
const UiState *App_GetState(void) { return &ui; }
static void initialize_ac_profile(UiProfile *profile)
{
    Bosch144State state;
    unsigned i;
    memset(profile,0,sizeof(*profile));
    Bosch144_Init(&state); /* Observed prefix + regenerated checksum: candidate. */
    (void)Bosch144_Generate(&state,&profile->keys[0]);
    for (i=1u;i<UI_KEY_COUNT;++i) profile->keys[i]=profile->keys[0];
    profile->bound_mask=31u; profile->kind=UI_PROFILE_BOSCH_ON;
}
static void send_signal(void *context,uint8_t profile,uint8_t key,const UiSignal *signal)
{
    Bosch144State next;
    UiProfile *p;
    uint8_t power;
    unsigned i;
    (void)context;
    ++g_app_diagnostics.tx_requests;
    if (profile>=UI_PROFILE_COUNT) { ++g_app_diagnostics.tx_rejected; return; }
    p=&ui.profiles[profile];
    if (p->kind==UI_PROFILE_RAW) {
        if (IrHardware_Send(signal)) ++g_app_diagnostics.tx_started;
        else ++g_app_diagnostics.tx_rejected;
        return;
    }
    if (IrHardware_Busy() || !Bosch144_StateFromSignal(&next,&p->keys[0])) {
        ++g_app_diagnostics.tx_rejected; return;
    }
    Bosch144_SetPower(&next,(uint8_t)(p->kind==UI_PROFILE_BOSCH_ON));
    if (!Bosch144_Button(&next,key) || !Bosch144_Generate(&next,&ui.captured) ||
        !IrHardware_Send(&ui.captured)) {
        ++g_app_diagnostics.tx_rejected; return;
    }
    ++g_app_diagnostics.tx_started;
    power=next.power;
    Bosch144_SetPower(&next,1u); /* Preserve settings even while power is OFF. */
    (void)Bosch144_Generate(&next,&p->keys[0]);
    for(i=1u;i<UI_KEY_COUNT;++i) p->keys[i]=p->keys[0];
    p->kind=power?UI_PROFILE_BOSCH_ON:UI_PROFILE_BOSCH_OFF;
    model_dirty=1u; selection_changed_at=milliseconds; ui.dirty=1u;
}
static int save_profile(void *context,uint8_t destination,const UiProfile *replacement)
{
    int ok;
    (void)context;
    if (IrHardware_Busy()) return 0;
    IrHardware_SetReceiveEnabled(0u);
    ok=ConfigStore_SaveReplacement(&store,ui.profiles,ui.active_profile,destination,replacement);
    if (ok) { saved_active=ui.active_profile; model_dirty=0u; ++g_app_diagnostics.store_save_ok; }
    else ++g_app_diagnostics.store_save_fail;
    return ok;
}
static void on_button(void *context,const ButtonEvent *event)
{
    UiPage old_page=ui.page;
    uint8_t old_error=ui.receive_error;
    Ui_Button((UiState *)context,event,milliseconds);
    if (old_error && !ui.receive_error && ui.page==UI_PAGE_RECEIVING)
        IrHardware_SetReceiveEnabled(0u);
    /* Start the visible result deadline AFTER synchronous Flash writes. */
    if (ui.page==UI_PAGE_RESULT && old_page!=UI_PAGE_RESULT) ui.result_at=milliseconds;
}
static void initialize_application(void)
{
    ConfigStoreBackend backend;
    UiCallbacks callbacks;
    milliseconds=0u;
    memset((void *)&g_app_diagnostics,0,sizeof(g_app_diagnostics));
    SystemCoreClockUpdate(); BoardKeys_Init(); OLED_Init();
    callbacks.send=send_signal; callbacks.save=save_profile; callbacks.context=0;
    Ui_Init(&ui,&callbacks,0u);
    FlashBackend_Init(&backend); ConfigStore_Init(&store,&backend);
    g_app_diagnostics.store_loaded=(uint32_t)ConfigStore_Load(&store,ui.profiles,&ui.active_profile);
    if (!g_app_diagnostics.store_loaded) {
        memset(ui.profiles,0,sizeof(ui.profiles)); ui.active_profile=0u;
    }
    model_dirty=0u;
    if (ui.profiles[0].kind==UI_PROFILE_RAW && !ui.profiles[0].bound_mask) {
        initialize_ac_profile(&ui.profiles[0]); model_dirty=1u;
    }
    saved_active=observed_active=ui.active_profile;
    selection_changed_at=last_service_ms=0u;
    Buttons_Init(&buttons,BoardKeys_ReadRaw(),0u,on_button,&ui);
    IrHardware_Init();
}
static void service_application(void)
{
    uint32_t now=milliseconds;
    int received;
    if (now!=last_service_ms) {
        last_service_ms=now;
        Buttons_Update(&buttons,BoardKeys_ReadRaw(),now);
        Ui_Tick(&ui,milliseconds);
    }
    IrHardware_SetReceiveEnabled((uint8_t)(ui.page==UI_PAGE_RECEIVING && !ui.receive_error));
    IrHardware_Poll(milliseconds);
    if (ui.page==UI_PAGE_RECEIVING && !ui.receive_error) {
        received=IrHardware_TryReceive(&ui.captured);
        if (received>0) {
            App_ReceiveSignal(&ui.captured); IrHardware_SetReceiveEnabled(0u);
        } else if (received<0) {
            ui.receive_error=1u; ui.dirty=1u;
            IrHardware_SetReceiveEnabled(0u);
        }
    }
    if (observed_active!=ui.active_profile) {
        observed_active=ui.active_profile; selection_changed_at=milliseconds;
    }
    g_app_diagnostics.selection_save_pending=(uint32_t)(saved_active!=ui.active_profile || model_dirty);
    if (g_app_diagnostics.selection_save_pending && ui.page==UI_PAGE_NORMAL &&
        !IrHardware_Busy() && (uint32_t)(milliseconds-selection_changed_at)>=1000u) {
        IrHardware_SetReceiveEnabled(0u);
        if (ConfigStore_Save(&store,ui.profiles,ui.active_profile)) {
            saved_active=ui.active_profile; model_dirty=0u; ++g_app_diagnostics.store_save_ok;
            g_app_diagnostics.selection_save_pending=0u;
        } else {
            ++g_app_diagnostics.store_save_fail;
            selection_changed_at=milliseconds; /* Bounded retry, not a hot loop. */
        }
    }
    if (ui.dirty) {
        UiPresenter_Build(&ui,&presentation); UiView_Render(&presentation.view); ui.dirty=0u;
    }
}
int main(void)
{
    initialize_application();
    if (SysTick_Config(SystemCoreClock/1000u)!=0u) {
        while (1) { /* Clock configuration fault. */ }
    }
    while (1) service_application();
}
