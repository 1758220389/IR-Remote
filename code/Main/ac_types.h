#ifndef AC_TYPES_H
#define AC_TYPES_H
#include <stdint.h>
#include "ui.h"
/* Compact payload bytes use UiSignal.timings[], one byte per slot. */
#define AC_MAX_BYTES 24u
#define AC_MODE_AUTO 0u
#define AC_MODE_COOL 1u
#define AC_MODE_DRY 2u
#define AC_MODE_HEAT 3u
#define AC_MODE_FAN 4u
#define AC_FAN_AUTO 0u
#define AC_FAN_LOW 1u
#define AC_FAN_MEDIUM 2u
#define AC_FAN_HIGH 3u
#define AC_IMPORT_STATE 1
#define AC_IMPORT_PARTIAL 2
/* power is separate so a dedicated OFF command never discards ON settings. */
typedef struct {
    uint16_t format;
    uint8_t length;
    uint8_t power;
    uint8_t raw[AC_MAX_BYTES];
} AcState;
typedef struct {
    uint8_t power, temp_half, mode, fan;
} AcStatus;
typedef uint16_t (*AcTimingReader)(void *context, uint16_t index);
#endif
