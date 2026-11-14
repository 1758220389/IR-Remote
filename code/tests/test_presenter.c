#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_presenter.h"
#include "ui_font.h"
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
    s.profiles[0].keys[0].format=UI_SIGNAL_BOSCH144;
    s.profiles[0].keys[0].length=18;
    s.profiles[0].keys[0].timings[2]=0xBF;
    s.profiles[0].keys[0].timings[4]=0xC0;
    s.profiles[0].keys[0].timings[13]=0x66;
    s.profiles[0].keys[0].timings[14]=0x20;
    UiPresenter_Build(&s,&p);assert(strstr(p.view.lines[1],"25.5C")!=0);check_rows(&p);
    s.profiles[0].kind=UI_PROFILE_RAW;
    for(page=0;page<=UI_PAGE_RESULT;++page)
        for(selection=0;selection<4;++selection)
            for(result=0;result<=UI_RESULT_SAVE_FAILED;++result) {
                s.page=(UiPage)page; s.selection=(uint8_t)selection;
                s.result=(UiResult)result; UiPresenter_Build(&s,&p); check_rows(&p);
            }
    s.page=UI_PAGE_SAVE_SELECT; s.dirty=0;
    Ui_Tick(&s,500); assert(s.dirty);
    s.page=UI_PAGE_SETTINGS; s.dirty=0;
    Ui_Tick(&s,1000); assert(s.dirty);
    puts("presenter: all page layouts/font coverage and settings/save blink passed");
    return 0;
}
