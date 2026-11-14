#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
int main(void)
{
    UiState state;
    UiSignal signal;
    memset(&signal,0,sizeof(signal));
    Ui_Init(&state,0,0u);
    state.page=UI_PAGE_RECEIVING;
    signal.length=199u;
    signal.carrier_khz=38u;
    assert(Ui_Receive(&state,&signal,0u)==1);
    puts("AC frame capacity: 199 timings accepted");
    return 0;
}
