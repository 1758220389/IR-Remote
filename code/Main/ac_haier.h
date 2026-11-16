#ifndef AC_HAIER_H
#define AC_HAIER_H
#include "ac_types.h"
/* Supported: HAIER 72-bit HSU07-HEA03 and YRW02 112-bit model A (Celsius).
 * No 160/176 support. Reader supplies microseconds, first/last are marks,
 * without leading/trailing silence. Exactly one complete frame is accepted.
 * Transmitter must append >=150000 us idle after the final mark; that silence
 * is deliberately NOT truncated into the uint16_t duration stream.
 * Decode timing tolerance is +/-25 percent of each nominal duration.
 * No implicit repeats or multi-frame captures: callers split captures at idle.
 * Payload is one native byte per timings[] slot, carrier_khz == 38.
 * Initialize AcState to zero before the first import.
 * Failed operations leave output/state unchanged. No heap or hardware calls.
 * Base72 OFF import preserves a previously learned matching ON state; without
 * one it returns AC_IMPORT_PARTIAL and leaves the destination unchanged.
 * Base72 other commands carry settings but not an explicit power bit: importing
 * them requires matching prior state (PARTIAL otherwise). ON establishes state.
 */
int AcHaier_Validate(const UiSignal *signal);
int AcHaier_Decode(AcTimingReader reader, void *context, uint16_t count,
                   UiSignal *out);
uint16_t AcHaier_DurationCount(const UiSignal *signal);
uint16_t AcHaier_Duration(const UiSignal *signal, uint16_t index);
int AcHaier_Import(AcState *state, const UiSignal *signal);
int AcHaier_GetStatus(const AcState *state, AcStatus *status);
int AcHaier_Button(AcState *state, uint8_t key);
int AcHaier_Encode(const AcState *state, UiSignal *signal);
#endif
