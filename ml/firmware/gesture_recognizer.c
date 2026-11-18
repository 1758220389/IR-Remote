#include "gesture_recognizer.h"
#include "gesture_model_v1.h"

void gesture_add(GestureAccumulator *state, const volatile SwdSample *sample)
{
    uint32_t t = sample->t_ms;
    int32_t z = sample->axes[5];
    unsigned axis;
    for (axis = 0; axis < 6; ++axis) {
        int16_t v = sample->axes[axis];
        if (v == -32768 || v == 32767) state->flags |= GESTURE_SATURATED;
    }
    if (state->count) {
        if (t - state->previous_ms != 10u) state->flags |= GESTURE_INTERVAL;
        state->sum_pairs += state->previous_z + z;
    }
    state->previous_ms = t;
    state->previous_z = z;
    ++state->count;
}

GesturePrediction gesture_finish(const GestureAccumulator *state,
                                 uint32_t duration_ms, uint32_t truncated)
{
    GesturePrediction result = {0};
    uint32_t count = state->count;
    unsigned i;

    result.label = GESTURE_REJECT;
    if (duration_ms < 150u) result.flags |= GESTURE_SHORT;
    if (truncated) result.flags |= GESTURE_TRUNCATED;
    if (count < 2u || count > 150u || duration_ms > 1500u ||
        count * 10u >= duration_ms + 10u || count * 10u + 10u <= duration_ms)
        result.flags |= GESTURE_COUNT;
    if (result.flags) return result;

    result.flags = state->flags;
    if (result.flags) return result;

    /* Linear form of the timestamp trapezoids after validating every 10 ms
       interval. The integer sum is bounded by 149 * 65536. */
    result.angle_deg = (float)state->sum_pairs * (0.005f / 131.0f)
                     - GESTURE_Z_BIAS_DPS * ((float)(count - 1u) * 0.01f);
    result.label = GESTURE_CCW;
    for (i = 0; i < 3; ++i) {
        result.scores[i] = gesture_weights[i] * result.angle_deg + gesture_intercepts[i];
        if (result.scores[i] > result.scores[result.label])
            result.label = i;
    }
    return result;
}

GesturePrediction gesture_predict(const volatile SwdSample *samples, unsigned count,
                                  uint32_t duration_ms, uint32_t truncated)
{
    GestureAccumulator state = {0};
    unsigned i;
    if (count <= 150u && duration_ms >= 150u && duration_ms <= 1500u && !truncated)
        for (i = 0; i < count; ++i) gesture_add(&state, &samples[i]);
    else
        state.count = count;
    return gesture_finish(&state, duration_ms, truncated);
}
