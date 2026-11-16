#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_presenter.h"
#include "ui_font.h"
#include "bosch144.h"
static void check_rows(const UiPresentation *p)
{
    unsigned row;
    for (row=0; row<4; ++row) {
        const unsigned char *s=(const unsigned char *)p->view.lines[row];
        unsigned width=0;
        while (*s) {
            if (*s < 128) { width+=8; ++s; }
            else {
                uint32_t cp=((uint32_t)(s[0]&15)<<12)|((uint32_t)(s[1]&63)<<6)|(s[2]&63);
                assert(UiFont_Find(cp)!=0); width+=16; s+=3;
            }
        }
        assert(width<=128);
    }
}
int main(void)
{
    UiState s; UiPresentation p; unsigned page, selection, result;
    Ui_Init(&s,0,0);
    s.page=UI_PAGE_DEVICE_TYPE;s.learn_ac=1u;
    UiPresenter_Build(&s,&p);
    assert(strcmp(p.view.lines[0],"是否为空调")==0);
    check_rows(&p);
    s.page=UI_PAGE_NORMAL;
    UiPresenter_Build(&s,&p);
    assert(strstr(p.view.lines[0],"1")!=0);
    s.page=UI_PAGE_SETTINGS; s.selection=0;
    UiPresenter_Build(&s,&p);
    assert(strcmp(p.view.lines[0],"学习")==0);
    assert(p.view.blink_rows==1);
    s.selection=3; UiPresenter_Build(&s,&p);
    assert(strstr(p.view.lines[0],"3")!=0);
    s.page=UI_PAGE_RECEIVING;s.receive_error=1;UiPresenter_Build(&s,&p);
    assert(strcmp(p.view.lines[0],"RX INVALID")==0);check_rows(&p);
    s.receive_error=0;s.page=UI_PAGE_NORMAL;
    s.profiles[0].kind=UI_PROFILE_BOSCH_ON;
    { Bosch144State state;Bosch144_Init(&state);
      assert(Bosch144_Generate(&state,&s.profiles[0].keys[0])); }
    UiPresenter_Build(&s,&p);assert(strstr(p.view.lines[1],"25.5C")!=0);check_rows(&p);
    s.profiles[0].kind=UI_PROFILE_RAW;
    for(page=0;page<=UI_PAGE_AC_UNSUPPORTED;++page)
        for(selection=0;selection<4;++selection)
            for(result=0;result<=UI_RESULT_SAVE_FAILED;++result) {
                s.page=(UiPage)page; s.selection=(uint8_t)selection;
                s.result=(UiResult)result; UiPresenter_Build(&s,&p); check_rows(&p);
            }
    s.page=UI_PAGE_RECEIVING;
    for(result=UI_RX_INVALID;result<=UI_RX_AMBIGUOUS;++result) {
        s.receive_error=(uint8_t)result;UiPresenter_Build(&s,&p);check_rows(&p);
    }
    s.receive_error=0;
    s.page=UI_PAGE_SAVE_SELECT; s.dirty=0;
    Ui_Tick(&s,500); assert(s.dirty);
    s.page=UI_PAGE_SETTINGS; s.dirty=0;
    Ui_Tick(&s,1000); assert(s.dirty);
    puts("presenter: all page layouts/font coverage and settings/save blink passed");
    return 0;
}
