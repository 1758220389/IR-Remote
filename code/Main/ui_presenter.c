/* UTF-8 source: all Chinese glyphs are in ui_font.c. */
#include "ui_presenter.h"
#include <stdio.h>
void UiPresenter_Build(const UiState *s, UiPresentation *p)
{
    unsigned i;
    for (i=0; i<4; ++i) p->view.lines[i]="";
    p->view.blink_rows=0;
    p->view.blink_visible=s->blink;
    switch (s->page) {
    case UI_PAGE_NORMAL:
        sprintf(p->title,"配置 %u",(unsigned)s->active_profile+1u);
        p->view.lines[0]=p->title;
        p->view.lines[2]="长按确认切换";
        break;
    case UI_PAGE_SETTINGS:
        if (s->selection==0u) p->view.lines[0]="学习";
        else {
            sprintf(p->title,"配置 %u",(unsigned)s->selection);
            p->view.lines[0]=p->title;
        }
        p->view.blink_rows=1u;
        p->view.lines[1]="1/2 上下选择";
        p->view.lines[2]="单击确认继续";
        p->view.lines[3]="长按确认返回";
        break;
    case UI_PAGE_RECEIVING:
        p->view.lines[0]="接收中";
        p->view.lines[1]="等待遥控信号";
        p->view.lines[2]=s->learned_mask ? "长按确认保存" : "长按确认退出";
        break;
    case UI_PAGE_RECEIVED:
        p->view.lines[0]="已接收";
        p->view.lines[1]="单击任意按键";
        p->view.lines[2]="完成绑定";
        p->view.lines[3]="按键 1-5";
        break;
    case UI_PAGE_SAVE_SELECT:
        p->view.lines[0]="保存至";
        sprintf(p->title,"配置 %u",(unsigned)s->save_profile+1u);
        p->view.lines[1]=p->title;
        p->view.blink_rows=2u;
        p->view.lines[2]="长按确认保存";
        p->view.lines[3]="长按其余键取消";
        break;
    case UI_PAGE_RESULT:
        if (s->result==UI_RESULT_CANCELLED) p->view.lines[0]="已取消";
        else if (s->result==UI_RESULT_SAVE_FAILED) p->view.lines[0]="SAVE FAILED";
        else {
            p->view.lines[0]="已保存";
            p->view.lines[1]=s->result==UI_RESULT_SAVED_FLASH ? "FLASH" : "RAM ONLY";
        }
        break;
    default: break;
    }
}
