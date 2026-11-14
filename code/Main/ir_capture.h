#ifndef IR_CAPTURE_H
#define IR_CAPTURE_H
#include "ui.h"
/* Raw durations are MICROSECONDS, alternating mark/space, beginning and
 * ending with a mark. Demodulated RX is active-low. Carrier cannot be
 * measured by a demodulator: learned signals explicitly assume 38 kHz. */
#define IR_CAPTURE_GAP_US 30000u
#define IR_CAPTURE_TIMEOUT_US 1000000u
#define IR_CAPTURE_MIN_US 100u
#define IR_CAPTURE_MIN_TIMINGS 7u
typedef struct {
    UiSignal signal;
    uint32_t started, last;
    uint8_t state, level;
} IrCaptureState;
void IrCapture_Init(IrCaptureState *s);
void IrCapture_Edge(IrCaptureState *s, uint32_t now_us, int high);
void IrCapture_Poll(IrCaptureState *s, uint32_t now_us, int high);
int IrCapture_Take(IrCaptureState *s, UiSignal *dest);
#endif
