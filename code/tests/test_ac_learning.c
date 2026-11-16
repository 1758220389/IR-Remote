#include "ui.h"
#include "bosch144.h"
#include "ac_codec.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void press(UiState *u,uint8_t key,ButtonEventType type)
{ ButtonEvent e;e.key=key;e.type=type;Ui_Button(u,&e,0u); }
static void enter_ac(UiState *u)
{
    Ui_Init(u,0,0u);press(u,5,BUTTON_LONG);press(u,5,BUTTON_SHORT);
    assert(u->page==UI_PAGE_DEVICE_TYPE && u->learn_ac);
    press(u,5,BUTTON_SHORT);assert(u->page==UI_PAGE_RECEIVING);
}
static void test_unknown_requires_explicit_raw_fallback(void)
{
    UiState u;
    UiSignal raw;
    enter_ac(&u);
    memset(&raw,0,sizeof(raw));raw.length=3u;raw.carrier_khz=38u;
    raw.timings[0]=1200u;raw.timings[1]=600u;raw.timings[2]=600u;
    assert(Ui_Receive(&u,&raw,0u));
    assert(u.page==UI_PAGE_AC_UNSUPPORTED && !u.learned_mask);
    press(&u,4,BUTTON_SHORT);
    assert(u.page==UI_PAGE_RECEIVED && !u.learn_ac);
    press(&u,2,BUTTON_SHORT);
    assert(u.learning.kind==UI_PROFILE_RAW && u.learned_mask==2u);
    assert(u.learning.keys[1].timings[0]==1200u);
}
static void test_receive_errors_never_offer_damaged_frame(void)
{
    UiState u;
    enter_ac(&u);
    Ui_ReceiveError(&u,UI_RX_INVALID);
    assert(u.page==UI_PAGE_RECEIVING && u.receive_error==UI_RX_INVALID);
    press(&u,4,BUTTON_SHORT);
    assert(u.receive_error==UI_RX_INVALID && !u.learned_mask);
    press(&u,5,BUTTON_SHORT);assert(!u.receive_error);
    Ui_ReceiveError(&u,UI_RX_UNSUPPORTED);
    assert(u.page==UI_PAGE_AC_UNSUPPORTED && !u.captured.length);
    press(&u,4,BUTTON_SHORT);
    assert(u.page==UI_PAGE_RECEIVING && !u.learn_ac && !u.learned_mask);
}
int main(void)
{
    UiState u;
    UiSignal signal;
    Bosch144State native;
    test_receive_errors_never_offer_damaged_frame();
    test_unknown_requires_explicit_raw_fallback();
    enter_ac(&u);
    Bosch144_Init(&native);assert(Bosch144_Generate(&native,&signal));
    assert(Ui_Receive(&u,&signal,0u));
    assert(u.page==UI_PAGE_AC_CONFIRM);
    assert(u.learned_mask==0u && u.profiles[0].bound_mask==0u);
    press(&u,5,BUTTON_SHORT);
    assert(u.page==UI_PAGE_SAVE_SELECT && u.learned_mask==31u);
    assert(u.learning.kind==UI_PROFILE_BOSCH_ON && Ac_ProfileValid(&u.learning));
    press(&u,2,BUTTON_SHORT);assert(u.save_profile==1u);
    press(&u,5,BUTTON_LONG);
    assert(u.result==UI_RESULT_SAVED_RAM && u.profiles[1].kind==UI_PROFILE_BOSCH_ON);
    assert(!u.profiles[0].bound_mask && u.active_profile==0u);
    puts("AC learning: question/identify/confirm/dynamic profile/selected replacement PASS");
    return 0;
}
