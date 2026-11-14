/* UTF-8 source: all Chinese glyphs are in ui_font.c. */
#include "ui_presenter.h"
#include <stdio.h>
static void present_bosch(const UiProfile *profile,UiPresentation *p)
{
    static const uint8_t temperatures[15]={2,0,4,12,8,24,28,20,16,48,52,36,32,40,44};
    const UiSignal *signal=&profile->keys[0];
    unsigned code,half=0u,i,mode,fan;
    const char *mode_name="?", *fan_name="?";
    if (signal->format!=UI_SIGNAL_BOSCH144 || signal->length!=18u) {
        p->view.lines[1]="AC DATA ERROR"; return;
    }
    code=((signal->timings[4]>>4)<<2)|((signal->timings[15]&16u)>>3)|
         ((signal->timings[14]&32u)>>5);
    for(i=0;i<15u;++i) if(temperatures[i]==(code&~1u)) half=(16u+i)*2u+(code&1u);
    mode=(((signal->timings[4]>>2)&3u)<<1)|(signal->timings[13]&1u);
    switch(mode) {
    case 0: mode_name="COOL";break; case 3: mode_name="DRY";break;
    case 5: mode_name="AUTO";break; case 6: mode_name="HEAT";break;
    case 2: mode_name="FAN";break; default:break;
    }
    fan=((signal->timings[2]>>5)<<6)|((signal->timings[13]>>1)&63u);
    switch(fan) {
    case 371: case 51: fan_name="AUTO";break; case 458:fan_name="20%";break;
    case 276:fan_name="40%";break; case 158:fan_name="60%";break;
    case 104:fan_name="80%";break; case 114:fan_name="100%";break;default:break;
    }
    sprintf(p->detail[0],"%s %u.%uC %s",profile->kind==UI_PROFILE_BOSCH_ON?"ON":"OFF",
            half/2u,(half&1u)?5u:0u,mode_name);
    sprintf(p->detail[1],"FAN %s",fan_name);
    p->view.lines[1]=p->detail[0];p->view.lines[2]=p->detail[1];
    p->view.lines[3]="HOLD5:SETTINGS";
}
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
        if (s->profiles[s->active_profile].kind!=UI_PROFILE_RAW)
            present_bosch(&s->profiles[s->active_profile],p);
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
        if (s->receive_error) {
            p->view.lines[0]="RX INVALID";
            p->view.lines[1]="5:RETRY";
            p->view.lines[2]="HOLD5:EXIT/SAVE";
            break;
        }
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
