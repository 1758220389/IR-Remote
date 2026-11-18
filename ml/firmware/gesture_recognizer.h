#ifndef GESTURE_RECOGNIZER_H
#define GESTURE_RECOGNIZER_H

#include <stdint.h>

typedef struct {
    uint32_t t_ms;
    int16_t axes[6];
} SwdSample;

enum { GESTURE_CCW, GESTURE_CW, GESTURE_NONE, GESTURE_REJECT };
enum {
    GESTURE_SHORT = 1, GESTURE_TRUNCATED = 2, GESTURE_COUNT = 4,
    GESTURE_INTERVAL = 8, GESTURE_SATURATED = 16
};

typedef struct {
    float angle_deg;
    float scores[3];
    uint32_t label;
    uint32_t flags;
} GesturePrediction;

/* The formal remote streams samples to avoid a second large RAM window. */
typedef struct {
    uint32_t count, previous_ms, flags;
    int32_t previous_z, sum_pairs;
} GestureAccumulator;

void gesture_add(GestureAccumulator *state, const volatile SwdSample *sample);
GesturePrediction gesture_finish(const GestureAccumulator *state,
                                 uint32_t duration_ms, uint32_t truncated);

GesturePrediction gesture_predict(const volatile SwdSample *samples, unsigned count,
                                  uint32_t duration_ms, uint32_t truncated);

#endif
