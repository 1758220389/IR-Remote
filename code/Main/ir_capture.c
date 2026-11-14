#include "ir_capture.h"
/* O(1): reset can run from an edge ISR; never clear the duration array. */
void IrCapture_Init(IrCaptureState *s) {
    s->signal.length=0u; s->signal.carrier_khz=38u;
    s->started=s->last=0u; s->state=0u; s->level=1u;
}
void IrCapture_Poll(IrCaptureState *s, uint32_t now, int high) {
    if (s->state==1u && ((uint32_t)(now-s->started)>=IR_CAPTURE_TIMEOUT_US ||
        (!s->level && (uint32_t)(now-s->last)>65535u))) s->state=3u;
    if ((s->state==1u || s->state==3u) && high && s->level &&
        (uint32_t)(now-s->last)>=IR_CAPTURE_GAP_US) {
        if (s->state==1u && s->signal.length>=IR_CAPTURE_MIN_TIMINGS)
            s->state=2u;
        else IrCapture_Init(s);
    }
}
void IrCapture_Edge(IrCaptureState *s, uint32_t now, int high) {
    /* Finish using the level BEFORE this edge; foreground polling may lag. */
    IrCapture_Poll(s, now, s->level);
    if (s->state==2u) return;
    if ((uint8_t)!!high==s->level) return;
    if (!s->state) {
        if (!high) { s->state=1u; s->started=s->last=now; s->level=0; }
        return;
    }
    if (s->state!=3u) {
        if ((uint32_t)(now-s->last)<IR_CAPTURE_MIN_US ||
            (uint32_t)(now-s->last)>65535u ||
            s->signal.length>=UI_SIGNAL_MAX_TIMINGS) s->state=3u;
        else s->signal.timings[s->signal.length++]=(uint16_t)(now-s->last);
    }
    s->last=now; s->level=(uint8_t)!!high;
}
int IrCapture_Take(IrCaptureState *s, UiSignal *dest) {
    if (s->state!=2u || !dest) return 0;
    s->signal.carrier_khz=38u;
    *dest=s->signal;
    IrCapture_Init(s);
    return 1;
}
