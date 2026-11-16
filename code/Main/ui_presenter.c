/* UTF-8 source: all Chinese glyphs are in ui_font.c. */
#include "ui_presenter.h"
#include "ac_codec.h"
#include <stdio.h>
static void present_ac(const UiSignal *signal,unsigned power,UiPresentation *p)
{
    static const char *modes[]={"AUTO","COOL","DRY","HEAT","FAN"};
    static const char *fans[]={"AUTO","LOW","MED","HIGH"};
    static const char *bosch_fans[]={"AUTO","20%","40%","60%","80%","100%"};
    AcState state;
    AcStatus status;
    const char *fan="?";
    if(Ac_Import(&state,signal)!=AC_IMPORT_STATE) {
        p->view.lines[1]="AC DATA ERROR";return;
    }
    if(power<=1u && state.power!=power && !Ac_Button(&state,5u)) {
        p->view.lines[1]="AC DATA ERROR";return;
    }
    if(!Ac_GetStatus(&state,&status) || status.mode>AC_MODE_FAN) {
        p->view.lines[1]="AC DATA ERROR";return;
    }
    if(signal->format==UI_SIGNAL_BOSCH144 && status.fan<6u) fan=bosch_fans[status.fan];
    else if(status.fan<4u) fan=fans[status.fan];
    if(status.temp_half)
        sprintf(p->detail[0],"%s %u.%uC %s",status.power?"ON":"OFF",
                status.temp_half/2u,(status.temp_half&1u)?5u:0u,modes[status.mode]);
    else sprintf(p->detail[0],"%s --.-C %s",status.power?"ON":"OFF",modes[status.mode]);
    sprintf(p->detail[1],"FAN %s",fan);
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
            present_ac(&s->profiles[s->active_profile].keys[0],
                       s->profiles[s->active_profile].kind==UI_PROFILE_BOSCH_ON ||
                       s->profiles[s->active_profile].kind==UI_PROFILE_AC_ON,p);
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
    case UI_PAGE_DEVICE_TYPE:
        p->view.lines[0]="是否为空调";
        p->view.lines[1]=s->learn_ac?"> 是  否":"是  > 否";
        p->view.lines[2]="1/2选择 5确认";
        p->view.lines[3]="长按确认返回";
        break;
    case UI_PAGE_AC_CONFIRM:
        sprintf(p->title,"AC %s",Ac_Name(s->captured.format));
        p->view.lines[0]=p->title;
        present_ac(&s->captured,2u,p);
        p->view.lines[3]="5:OK 1:RETRY";
        break;
    case UI_PAGE_AC_UNSUPPORTED:
        p->view.lines[0]="未识别协议";
        p->view.lines[1]=s->captured.length?"RAW固定信号":"RAW容量有限";
        p->view.lines[2]="5:RETRY 4:RAW";
        p->view.lines[3]="长按确认退出";
        break;
    case UI_PAGE_RECEIVING:
        if (s->receive_error) {
            if(s->receive_error==UI_RX_NEED_ON) p->view.lines[0]="需要开机状态";
            else if(s->receive_error==UI_RX_AMBIGUOUS) p->view.lines[0]="协议匹配不唯一";
            else p->view.lines[0]="RX INVALID";
            p->view.lines[1]=s->receive_error==UI_RX_AMBIGUOUS?"原遥控稍长按":"5:RETRY";
            p->view.lines[2]=s->receive_error==UI_RX_AMBIGUOUS?"5:RETRY":"HOLD5:EXIT/SAVE";
            p->view.lines[3]="长按确认退出";
            break;
        }
        p->view.lines[0]=s->learn_ac?"空调识别中":"接收中";
        p->view.lines[1]=s->learn_ac?"请发开机状态":"等待遥控信号";
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
