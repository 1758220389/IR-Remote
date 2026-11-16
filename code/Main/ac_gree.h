#ifndef AC_GREE_H
#define AC_GREE_H
#include "ac_types.h"
/* LGPL-2.1-or-later; see third_party/GREE-NOTICE.txt.
 * Standard 64-bit GREE Celsius frames, YAW1F/YBOFB power conventions.
 * A new profile requires an ON import: OFF cannot identify the ON model bit.
 * OFF import into an existing valid GREE state returns AC_IMPORT_PARTIAL,
 * changing only power (saved ON settings/model marker are retained).
 * Initialize AcState before importing a potentially OFF frame.
 * Fahrenheit, timers, economy/YX1FSF extensions and non-64-bit variants
 * are rejected; optional swing/light/sleep/turbo/XFan/IFeel/WiFi fields
 * in the supported layout are preserved, not actively controlled.
 * The native nibble checksum cannot detect corruption in every data bit.
 * AUTO locks temperature to 25 C; DRY locks fan to LOW.
 * Decode accepts exactly one mark-first/mark-last 139-duration envelope.
 * On failure all output/state parameters are unchanged. No hardware I/O.
 */
int AcGree_Validate(const UiSignal *signal);
int AcGree_Decode(AcTimingReader reader, void *context, uint16_t count,
                  UiSignal *output);
uint16_t AcGree_DurationCount(const UiSignal *signal);
uint16_t AcGree_Duration(const UiSignal *signal, uint16_t index);
int AcGree_Import(AcState *state, const UiSignal *signal);
int AcGree_GetStatus(const AcState *state, AcStatus *status);
int AcGree_Button(AcState *state, uint8_t key);
int AcGree_Encode(const AcState *state, UiSignal *signal);
#endif
