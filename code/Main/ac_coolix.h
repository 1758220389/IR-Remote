#ifndef AC_COOLIX_H
#define AC_COOLIX_H
#include "ac_types.h"
/* Payload is three big-endian wire bytes. Decode input starts at header mark;
 * final idle space may be omitted. Output unchanged on failure.
 * Import returns AC_IMPORT_STATE or PARTIAL; PARTIAL leaves state untouched.
 * Native Celsius state supports keys 1/2 = +/-1C, 3 = five-mode cycle,
 * 4 = fan cycle, 5 = power toggle. Fan-only temp_half is 0 (unavailable),
 * temperature keys fail unchanged; native Fan -> Auto uses 30C, as upstream.
 * Dedicated OFF/standalone controls cannot establish a full state. raw[]
 * retains the ON payload even when state.power is 0; Encode then emits OFF.
 * DurationCount is 200 (byte + complement, one full-frame repeat). Final
 * idle is 65535us (API limit, above protocol minimum). Extended Fahrenheit,
 * zone-follow fan and COOLIX48 timer variants are deliberately rejected.
 * B27BE0 OFF is indistinguishable from Bosch dedicated OFF.
 * See third_party/MIDEA-COOLIX-NOTICE.txt for scope and fixture provenance. */
int AcCoolix_Validate(const UiSignal *signal);
int AcCoolix_Decode(AcTimingReader reader, void *context, uint16_t count, UiSignal *signal);
uint16_t AcCoolix_DurationCount(const UiSignal *signal);
uint16_t AcCoolix_Duration(const UiSignal *signal, uint16_t index);
int AcCoolix_Import(AcState *state, const UiSignal *signal);
int AcCoolix_GetStatus(const AcState *state, AcStatus *status);
int AcCoolix_Button(AcState *state, uint8_t key);
int AcCoolix_Encode(const AcState *state, UiSignal *signal);
#endif
