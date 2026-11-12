#include "stm32f10x.h"
#include "OLED.h"
#include "app.h"
#include "board_keys.h"
#include "ui_presenter.h"
#include <string.h>

static volatile uint32_t milliseconds;
static UiState ui;
static ButtonsState buttons;
static UiPresentation presentation;

void App_Tick1ms(void) { ++milliseconds; }
int App_ReceiveSignal(const UiSignal *signal)
{
    return Ui_Receive(&ui, signal, milliseconds);
}
void App_SetBackend(const UiCallbacks *callbacks)
{
    if (callbacks != 0) ui.callbacks = *callbacks;
    else memset(&ui.callbacks, 0, sizeof(ui.callbacks));
}
const UiState *App_GetState(void) { return &ui; }
static void on_button(void *context, const ButtonEvent *event)
{
    Ui_Button((UiState *)context, event, milliseconds);
}
int main(void)
{
    uint32_t now, last;
    SystemCoreClockUpdate();
    BoardKeys_Init();
    OLED_Init();
    Ui_Init(&ui, 0, 0u);
    Buttons_Init(&buttons, BoardKeys_ReadRaw(), 0u, on_button, &ui);
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        while (1) { /* Clock fault: do not run with false timing. */ }
    }
    last = milliseconds;
    while (1) {
        now = milliseconds;
        if (now != last) {
            last = now;
            Buttons_Update(&buttons, BoardKeys_ReadRaw(), now);
            Ui_Tick(&ui, now);
        }
        /* Future IR backend drains captured signals here, not in an ISR. */
        if (ui.dirty) {
            UiPresenter_Build(&ui, &presentation);
            UiView_Render(&presentation.view);
            ui.dirty = 0u;
        }
    }
}
