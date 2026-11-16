#ifndef AC_MIDEA_H
#define AC_MIDEA_H
#include "ac_types.h"
/* Payload is six big-endian wire bytes. Decode input starts at header mark;
 * final idle space may be omitted. Output unchanged on failure.
 * Import returns AC_IMPORT_STATE or PARTIAL; PARTIAL leaves state untouched.
 * Normal A1 Celsius/Fahrenheit states support keys 1/2 = native +/-1 degree,
 * 3 = Auto/Cool/Dry/Heat/Fan cycle, 4 = fan cycle, 5 = power toggle.
 * Fahrenheit status is rounded to nearest half Celsius, not converted in raw.
 * Known A2 specials / A4 FollowMe can stream but cannot become full state.
 * DurationCount is 200, including normal + inverted payload. Final idle is
 * 65535us (API limit, above protocol minimum); no zero-mark gap splitting.
 * See third_party/MIDEA-COOLIX-NOTICE.txt for scope and fixture provenance. */
int AcMidea_Validate(const UiSignal *signal);
int AcMidea_Decode(AcTimingReader reader, void *context, uint16_t count, UiSignal *signal);
uint16_t AcMidea_DurationCount(const UiSignal *signal);
uint16_t AcMidea_Duration(const UiSignal *signal, uint16_t index);
int AcMidea_Import(AcState *state, const UiSignal *signal);
int AcMidea_GetStatus(const AcState *state, AcStatus *status);
int AcMidea_Button(AcState *state, uint8_t key);
int AcMidea_Encode(const AcState *state, UiSignal *signal);
#endif
