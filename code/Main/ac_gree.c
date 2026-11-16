/* LGPL-2.1-or-later. Protocol adapted from IRremoteESP8266 ir_Gree
 * (Ville Skytta, David Conran). See third_party/GREE-NOTICE.txt. */
#include "ac_gree.h"
#include <string.h>
static uint8_t checksum(const uint8_t *raw)
{
    unsigned int i, sum;
    sum = 10u;
    for (i=0;i<4u;i++) sum += raw[i] & 15u;
    for (i=4;i<7u;i++) sum += raw[i] >> 4;
    return (uint8_t)(sum & 15u);
}
static int valid_raw(const uint8_t *r)
{
    unsigned int v;
    if ((r[0]&7u)>4u || r[1]>14u || (r[2]&15u)!=0u ||
        r[3]!=0x50u || (r[4]&0x80u)!=0u ||
        (r[5]&0xb8u)!=0x20u || r[6]!=0u || (r[7]&15u)!=0u)
        return 0;
    if ((r[0]&7u)==0u && r[1]!=9u) return 0;
    if ((r[0]&7u)==2u && ((r[0]>>4)&3u)!=1u) return 0;
    if (!(r[0]&8u) && (r[2]&0x40u)) return 0;
    v = r[4]&15u;
    if (v>7u && v!=9u && v!=11u) return 0;
    if (((r[4]>>4)&7u)>6u) return 0;
    return (r[7]>>4)==checksum(r);
}
int AcGree_Validate(const UiSignal *signal)
{
    uint8_t raw[8];
    unsigned int i;
    if (!signal || signal->format!=UI_SIGNAL_GREE ||
        signal->length!=8u || signal->carrier_khz!=38u) return 0;
    for (i=0;i<8u;i++) {
        if (signal->timings[i]>255u) return 0;
        raw[i]=(uint8_t)signal->timings[i];
    }
    return valid_raw(raw);
}
uint16_t AcGree_DurationCount(const UiSignal *signal)
{
    return AcGree_Validate(signal) ? 139u : 0u;
}
uint16_t AcGree_Duration(const UiSignal *signal, uint16_t index)
{
    unsigned int slot, bit;
    if (index>=139u || !AcGree_Validate(signal)) return 0;
    if (index==0u) return 9000;
    if (index==1u) return 4500;
    if (index==73u) return 19980;
    if (!(index&1u)) return 620;
    slot=(index-3u)/2u;
    if (slot>=32u && slot<35u) bit=slot==33u;
    else {
        if (slot>=35u) slot-=4u;
        bit=(signal->timings[slot/8u]>>(slot%8u))&1u;
    }
    return bit ? 1600u : 540u;
}
static int valid_state(const AcState *state)
{
    return state && state->format==UI_SIGNAL_GREE && state->length==8u &&
           state->power<=1u && (state->raw[0]&8u) && valid_raw(state->raw);
}
int AcGree_Import(AcState *state, const UiSignal *signal)
{
    unsigned int i;
    if (!state || !AcGree_Validate(signal)) return 0;
    if (!(signal->timings[0]&8u)) {
        /* OFF has no model marker: never invent the model for a new profile. */
        if (!valid_state(state)) return 0;
        state->power=0;
        return AC_IMPORT_PARTIAL;
    }
    memset(state,0,sizeof(*state));
    state->format=UI_SIGNAL_GREE;
    state->length=8;
    state->power=1;
    for (i=0;i<8u;i++) state->raw[i]=(uint8_t)signal->timings[i];
    return AC_IMPORT_STATE;
}
int AcGree_GetStatus(const AcState *state, AcStatus *status)
{
    unsigned int mode;
    if (!status || !valid_state(state)) return 0;
    mode=state->raw[0]&7u;
    status->power=state->power;
    status->temp_half=(uint8_t)((16u+state->raw[1])*2u);
    status->mode=(uint8_t)(mode==3u ? AC_MODE_FAN :
                          mode==4u ? AC_MODE_HEAT : mode);
    status->fan=(uint8_t)((state->raw[0]>>4)&3u);
    return 1;
}
int AcGree_Encode(const AcState *state, UiSignal *signal)
{
    uint8_t raw[8];
    unsigned int i;
    if (!signal || !valid_state(state)) return 0;
    memcpy(raw,state->raw,8);
    if (!state->power) {
        raw[0]&=(uint8_t)~8u;
        raw[2]&=(uint8_t)~0x40u;
    }
    raw[7]=(uint8_t)(checksum(raw)<<4);
    memset(signal,0,sizeof(*signal));
    signal->format=UI_SIGNAL_GREE;
    signal->length=8;
    signal->carrier_khz=38;
    for (i=0;i<8u;i++) signal->timings[i]=raw[i];
    return 1;
}
int AcGree_Button(AcState *state, uint8_t key)
{
    unsigned int mode, fan;
    if (!valid_state(state) || key<1u || key>5u) return 0;
    mode=state->raw[0]&7u;
    if (key==5u) {
        state->power^=1u;
        return 1;
    }
    if (key==1u && mode!=0u && state->raw[1]<14u) state->raw[1]++;
    if (key==2u && mode!=0u && state->raw[1]>0u) state->raw[1]--;
    if (key==3u) {
        mode=mode==2u ? 4u : mode==4u ? 3u : mode==3u ? 0u : mode+1u;
        state->raw[0]=(uint8_t)((state->raw[0]&0xf8u)|mode);
        if (mode==0u) state->raw[1]=9;
    }
    if (key==4u) {
        fan=(((state->raw[0]>>4)&3u)+1u)%4u;
        state->raw[0]=(uint8_t)((state->raw[0]&0xcfu)|(fan<<4));
    }
    if (mode==2u) state->raw[0]=(uint8_t)((state->raw[0]&0xcfu)|0x10u);
    state->raw[7]=(uint8_t)(checksum(state->raw)<<4);
    return 1;
}
static int near_time(uint16_t actual, uint16_t nominal)
{
    return (unsigned long)actual*4ul >= (unsigned long)nominal*3ul &&
           (unsigned long)actual*4ul <= (unsigned long)nominal*5ul;
}
int AcGree_Decode(AcTimingReader reader, void *context, uint16_t count,
                  UiSignal *output)
{
    uint8_t raw[8];
    uint16_t pos, space;
    unsigned int bit, value, byte_index;
    if (!reader || !output || count!=139u) return 0;
    if (!near_time(reader(context,0),9000) ||
        !near_time(reader(context,1),4500)) return 0;
    memset(raw,0,sizeof(raw));
    pos=2;
    for (bit=0;bit<67u;bit++) {
        if (!near_time(reader(context,pos++),620)) return 0;
        space=reader(context,pos++);
        if (near_time(space,540)) value=0;
        else if (near_time(space,1600)) value=1;
        else return 0;
        if (bit>=32u && bit<35u) {
            if (value!=(bit==33u ? 1u : 0u)) return 0;
        } else {
            byte_index=bit<32u ? bit : bit-3u;
            raw[byte_index/8u] |= (uint8_t)(value<<(byte_index%8u));
        }
        if (bit==34u) {
            if (!near_time(reader(context,pos++),620) ||
                !near_time(reader(context,pos++),19980)) return 0;
        }
    }
    if (!near_time(reader(context,pos),620) || !valid_raw(raw)) return 0;
    memset(output,0,sizeof(*output));
    output->format=UI_SIGNAL_GREE;
    output->length=8;
    output->carrier_khz=38;
    for (bit=0;bit<8u;bit++) output->timings[bit]=raw[bit];
    return 1;
}
