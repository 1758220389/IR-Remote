#include "ac_codec.h"
#include "bosch144.h"
#include <string.h>
#ifndef AC_BOSCH_ONLY
#include "ac_gree.h"
#include "ac_midea.h"
#include "ac_coolix.h"
#include "ac_haier.h"
#endif
static int bos_import(AcState *s,const UiSignal *x)
{
    unsigned i;
    if(!Bosch144_ValidateSignal(x)) return 0;
    s->format=UI_SIGNAL_BOSCH144;s->length=(uint8_t)x->length;
    s->power=(uint8_t)(x->length==18u);
    for(i=0;i<x->length;++i) s->raw[i]=(uint8_t)x->timings[i];
    return s->power?AC_IMPORT_STATE:AC_IMPORT_PARTIAL;
}
static int bos_native(const AcState *s,Bosch144State *b)
{
    if(!s || s->format!=UI_SIGNAL_BOSCH144 || s->length!=18u) return 0;
    memcpy(b->raw,s->raw,18);b->power=s->power;return 1;
}
static int bos_status(const AcState *s,AcStatus *out)
{
    Bosch144State b;
    uint8_t mode;
    uint16_t fan;
    if(!out || !bos_native(s,&b)) return 0;
    out->power=s->power;out->temp_half=Bosch144_GetTempHalf(&b);
    mode=Bosch144_GetMode(&b);fan=Bosch144_GetFan(&b);
    switch(mode) {
    case BOSCH144_MODE_AUTO:out->mode=AC_MODE_AUTO;break;
    case BOSCH144_MODE_COOL:out->mode=AC_MODE_COOL;break;
    case BOSCH144_MODE_DRY:out->mode=AC_MODE_DRY;break;
    case BOSCH144_MODE_HEAT:out->mode=AC_MODE_HEAT;break;
    case BOSCH144_MODE_FAN:out->mode=AC_MODE_FAN;break;
    default:return 0;
    }
    switch(fan) {
    case BOSCH144_FAN_AUTO:case 51u:out->fan=0u;break;
    case BOSCH144_FAN_20:out->fan=1u;break;
    case BOSCH144_FAN_40:out->fan=2u;break;
    case BOSCH144_FAN_60:out->fan=3u;break;
    case BOSCH144_FAN_80:out->fan=4u;break;
    case BOSCH144_FAN_100:out->fan=5u;break;
    default:return 0;
    }
    return out->temp_half!=0u;
}
static int bos_button(AcState *s,uint8_t key)
{
    Bosch144State b;
    if(!bos_native(s,&b) || !Bosch144_Button(&b,key)) return 0;
    memcpy(s->raw,b.raw,18);s->power=b.power;return 1;
}
static int bos_encode(const AcState *s,UiSignal *out)
{
    Bosch144State b;
    return bos_native(s,&b) && Bosch144_Generate(&b,out);
}
typedef struct {
    uint16_t first,last;
    int (*valid)(const UiSignal *);
    int (*decode)(AcTimingReader,void *,uint16_t,UiSignal *);
    uint16_t (*count)(const UiSignal *);
    uint16_t (*duration)(const UiSignal *,uint16_t);
    int (*import_state)(AcState *,const UiSignal *);
    int (*status)(const AcState *,AcStatus *);
    int (*button)(AcState *,uint8_t);
    int (*encode)(const AcState *,UiSignal *);
} Codec;
static const Codec codecs[]={
    {1u,1u,Bosch144_ValidateSignal,Bosch144_Decode,Bosch144_DurationCount,
     Bosch144_Duration,bos_import,bos_status,bos_button,bos_encode},
#ifndef AC_BOSCH_ONLY
    {2u,2u,AcGree_Validate,AcGree_Decode,AcGree_DurationCount,AcGree_Duration,
     AcGree_Import,AcGree_GetStatus,AcGree_Button,AcGree_Encode},
    {3u,3u,AcMidea_Validate,AcMidea_Decode,AcMidea_DurationCount,AcMidea_Duration,
     AcMidea_Import,AcMidea_GetStatus,AcMidea_Button,AcMidea_Encode},
    {4u,4u,AcCoolix_Validate,AcCoolix_Decode,AcCoolix_DurationCount,AcCoolix_Duration,
     AcCoolix_Import,AcCoolix_GetStatus,AcCoolix_Button,AcCoolix_Encode},
    {5u,8u,AcHaier_Validate,AcHaier_Decode,AcHaier_DurationCount,AcHaier_Duration,
     AcHaier_Import,AcHaier_GetStatus,AcHaier_Button,AcHaier_Encode},
#endif
};
static const Codec *codec_for(uint16_t format)
{
    unsigned i;
    for(i=0;i<sizeof(codecs)/sizeof(codecs[0]);++i)
        if(format>=codecs[i].first && format<=codecs[i].last) return &codecs[i];
    return 0;
}
int Ac_Validate(const UiSignal *x)
{
    const Codec *c;
    if(!x) return 0;
    c=codec_for(x->format);return c && c->valid(x);
}
uint16_t Ac_DurationCount(const UiSignal *x)
{
    const Codec *c;
    if(!x) return 0;
    c=codec_for(x->format);return c?c->count(x):0u;
}
uint16_t Ac_Duration(const UiSignal *x,uint16_t index)
{
    const Codec *c;
    if(!x) return 0;
    c=codec_for(x->format);return c?c->duration(x,index):0u;
}
int Ac_Import(AcState *s,const UiSignal *x)
{
    const Codec *c;
    AcState fresh;
    int result;
    if(!s || !x) return 0;
    c=codec_for(x->format);
    if(!c) return 0;
    /* Identification never guesses prior settings from uninitialized stack. */
    memset(&fresh,0,sizeof(fresh));
    result=c->import_state(&fresh,x);
    if(result>0) *s=fresh;
    return result;
}
int Ac_GetStatus(const AcState *s,AcStatus *out)
{
    const Codec *c;
    if(!s || !out) return 0;
    c=codec_for(s->format);
    return c?c->status(s,out):0;
}
int Ac_Button(AcState *s,uint8_t key)
{
    const Codec *c;
    if(!s || key<1u || key>5u) return 0;
    c=codec_for(s->format);
    return c?c->button(s,key):0;
}
int Ac_Encode(const AcState *s,UiSignal *out)
{
    const Codec *c;
    if(!s || !out) return 0;
    c=codec_for(s->format);
    return c?c->encode(s,out):0;
}
static int near_duration(uint16_t actual,uint16_t nominal)
{ return actual>=(uint32_t)nominal*3u/4u && actual<=(uint32_t)nominal*5u/4u; }
int Ac_LooksSupported(AcTimingReader r,void *ctx,uint16_t n)
{
    uint16_t mark,space;
    if(!r || n<7u) return 0;
    mark=r(ctx,0);space=r(ctx,1);
    if(near_duration(mark,4400u) && near_duration(space,4400u)) return 1;
    if(n>=80u && near_duration(mark,9000u) && near_duration(space,4500u)) return 1;
    return n>=80u && near_duration(mark,3000u) && near_duration(space,3000u);
}
int Ac_Detect(AcTimingReader r,void *ctx,uint16_t n,UiSignal *out)
{
    unsigned i,matches=0u;
    uint8_t bosch_off=0u;
    UiSignal candidate;
    AcState imported;
    if(!r || !out || !n || !(n&1u)) return AC_DETECT_INVALID;
    memset(&candidate,0,sizeof(candidate));
    for(i=0;i<sizeof(codecs)/sizeof(codecs[0]);++i) {
        if(codecs[i].decode(r,ctx,n,&candidate)) {
            ++matches;
            if(candidate.format==UI_SIGNAL_BOSCH144 && candidate.length==12u) bosch_off=1u;
        }
    }
    /* Identical Bosch/Coolix OFF waveform is safe to replay, but Import marks
     * it partial, so it cannot establish an AC model or silently pick a brand. */
    if(matches>1u && bosch_off && Bosch144_Decode(r,ctx,n,&candidate)) {
        *out=candidate;return AC_DETECT_MATCH;
    }
    if(matches>1u) return AC_DETECT_AMBIGUOUS;
    if(matches==1u) {
        /* A B2 COOLIX state with only one/two blocks can also be the valid
         * prefix of an interrupted Bosch extended frame. Three identical
         * COOLIX blocks cannot be that prefix (Bosch block3 starts D5).
         * Ask for another/longer press instead of silently downgrading it. */
        if(candidate.format==UI_SIGNAL_COOLIX && candidate.timings[0]==0xB2u &&
           (n==99u || n==199u) && Ac_Import(&imported,&candidate)==AC_IMPORT_STATE)
            return AC_DETECT_AMBIGUOUS;
        *out=candidate;return AC_DETECT_MATCH;
    }
    return Ac_LooksSupported(r,ctx,n)?AC_DETECT_INVALID:AC_DETECT_UNKNOWN;
}
const char *Ac_Name(uint16_t format)
{
    switch(format) {
    case UI_SIGNAL_BOSCH144:return "BOSCH144";
    case UI_SIGNAL_GREE:return "GREE";
    case UI_SIGNAL_MIDEA:return "MIDEA";
    case UI_SIGNAL_COOLIX:return "COOLIX";
    case UI_SIGNAL_HAIER:return "HAIER72";
    case UI_SIGNAL_HAIER_YRW02:return "HAIER-YRW02";
    case UI_SIGNAL_HAIER160:return "HAIER160";
    case UI_SIGNAL_HAIER176:return "HAIER176";
    default:return "UNKNOWN";
    }
}
int Ac_ProfileValid(const UiProfile *p)
{
    AcState state;
    AcStatus status;
    if(!p || !p->kind || p->kind>UI_PROFILE_LAST || p->bound_mask!=31u) return 0;
    if((p->kind<=UI_PROFILE_BOSCH_OFF)!=(p->keys[0].format==UI_SIGNAL_BOSCH144)) return 0;
    return Ac_Import(&state,&p->keys[0])==AC_IMPORT_STATE && state.power &&
           Ac_GetStatus(&state,&status);
}
int Ac_ProfileLoad(AcState *s,const UiProfile *p)
{
    if(!s || !Ac_ProfileValid(p)) return 0;
    if(Ac_Import(s,&p->keys[0])!=AC_IMPORT_STATE) return 0;
    if(s->power!=(uint8_t)(p->kind==UI_PROFILE_BOSCH_ON || p->kind==UI_PROFILE_AC_ON))
        return Ac_Button(s,5u); /* Also updates native power bits/checksum. */
    return 1;
}
void Ac_ProfileCommit(UiProfile *p,const AcState *s)
{
    AcState on;
    unsigned i;
    if(!p || !s) return;
    on=*s;
    /* Store a canonical ON snapshot. Haier72's last button command is not
     * enough to initialize power after reboot; a power cycle sets ON command
     * without transmitting it. Native power/checksum bookkeeping stays valid. */
    if(on.power && !Ac_Button(&on,5u)) return;
    if(!Ac_Button(&on,5u)) return;
    if(!Ac_Encode(&on,&p->keys[0])) return;
    for(i=1u;i<UI_KEY_COUNT;++i) p->keys[i]=p->keys[0];
    p->bound_mask=31u;
    if(s->format==UI_SIGNAL_BOSCH144) p->kind=s->power?UI_PROFILE_BOSCH_ON:UI_PROFILE_BOSCH_OFF;
    else p->kind=s->power?UI_PROFILE_AC_ON:UI_PROFILE_AC_OFF;
}
int Ac_ProfileFromSignal(UiProfile *p,const UiSignal *signal)
{
    AcState s;
    AcStatus status;
    if(!p || Ac_Import(&s,signal)!=AC_IMPORT_STATE || !s.power ||
       !Ac_GetStatus(&s,&status)) return 0;
    memset(p,0,sizeof(*p));Ac_ProfileCommit(p,&s);return Ac_ProfileValid(p);
}
