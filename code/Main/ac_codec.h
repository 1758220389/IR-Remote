#ifndef AC_CODEC_H
#define AC_CODEC_H
#include "ac_types.h"
#define AC_DETECT_UNKNOWN 0
#define AC_DETECT_MATCH 1
#define AC_DETECT_INVALID (-1)
#define AC_DETECT_AMBIGUOUS (-2)
int Ac_Validate(const UiSignal *signal);
int Ac_Detect(AcTimingReader reader, void *context, uint16_t count, UiSignal *out);
int Ac_LooksSupported(AcTimingReader reader, void *context, uint16_t count);
uint16_t Ac_DurationCount(const UiSignal *signal);
uint16_t Ac_Duration(const UiSignal *signal, uint16_t index);
int Ac_Import(AcState *state, const UiSignal *signal);
int Ac_GetStatus(const AcState *state, AcStatus *status);
int Ac_Button(AcState *state, uint8_t key);
int Ac_Encode(const AcState *state, UiSignal *out);
const char *Ac_Name(uint16_t format);
int Ac_ProfileFromSignal(UiProfile *profile, const UiSignal *signal);
int Ac_ProfileLoad(AcState *state, const UiProfile *profile);
int Ac_ProfileValid(const UiProfile *profile);
void Ac_ProfileCommit(UiProfile *profile, const AcState *state);
#endif
