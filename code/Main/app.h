#ifndef APP_H
#define APP_H
#include "ui.h"
typedef struct {
    uint32_t store_loaded, store_save_ok, store_save_fail;
    uint32_t rx_frames, last_rx_length, tx_requests, tx_started, tx_rejected;
    uint32_t selection_save_pending;
} AppDiagnostics;
extern volatile AppDiagnostics g_app_diagnostics;
/* Foreground only: ISR captures are consumed by the application loop. */
int App_ReceiveSignal(const UiSignal *signal);
void App_SetBackend(const UiCallbacks *callbacks);
const UiState *App_GetState(void);
/* Called only by SysTick_Handler. */
void App_Tick1ms(void);
#endif
