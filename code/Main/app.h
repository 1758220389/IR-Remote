#ifndef APP_H
#define APP_H
#include "ui.h"
/* Foreground only: the future IR driver must queue ISR captures before calling. */
int App_ReceiveSignal(const UiSignal *signal);
void App_SetBackend(const UiCallbacks *callbacks);
const UiState *App_GetState(void);
/* Called only by SysTick_Handler. */
void App_Tick1ms(void);
#endif
