#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "bosch144.h"
int main(void) {
    UiState s; UiSignal x;
    Ui_Init(&s,0,0);memset(&x,0,sizeof(x));
    s.page=UI_PAGE_RECEIVING;
    { Bosch144State native;Bosch144_Init(&native);
      assert(Bosch144_Generate(&native,&x)); }
    assert(Ui_Receive(&s,&x,0));
    s.page=UI_PAGE_RECEIVING;x.format=99;assert(!Ui_Receive(&s,&x,0));
    x.format=UI_SIGNAL_BOSCH144;x.length=17;assert(!Ui_Receive(&s,&x,0));
    { Bosch144State native;Bosch144_Init(&native);Bosch144_SetPower(&native,0u);
      assert(Bosch144_Generate(&native,&x)); }
    assert(Ui_Receive(&s,&x,0));
    { ButtonEvent event; event.key=5; event.type=BUTTON_SHORT;
      s.page=UI_PAGE_RECEIVING; s.receive_error=1; s.dirty=0;
      Ui_Button(&s,&event,0); assert(!s.receive_error && s.dirty); }
    assert(sizeof(UiState)<12000u);
    puts("explicit compact signal format admission and SRAM budget: PASS");return 0;
}
