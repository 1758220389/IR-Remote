#ifndef BUTTONS_LOGIC_H
#define BUTTONS_LOGIC_H
#include <stdint.h>
#define BUTTON_COUNT 5u
#define BUTTON_MASK 31u
#define BUTTON_DEBOUNCE_MS 20u
#define BUTTON_LONG_MS 1000u
typedef enum { BUTTON_SHORT = 0, BUTTON_LONG = 1 } ButtonEventType;
typedef struct { uint8_t key; ButtonEventType type; } ButtonEvent;
typedef void (*ButtonEventCallback)(void *context, const ButtonEvent *event);
typedef struct {
    uint32_t changed_at[BUTTON_COUNT];
    uint32_t pressed_at[BUTTON_COUNT];
    uint8_t raw_mask, stable_mask, blocked_mask, long_mask;
    ButtonEventCallback callback;
    void *context;
} ButtonsState;
/* Raw bits 0..4 are logical pressed levels (not GPIO voltage levels).
 * Initial held keys are ignored until a debounced release. Call regularly.
 * Time is monotonic uint32_t milliseconds; wraparound is supported.
 * Short is emitted only on debounced release, long once after 1000 ms
 * from the debounced press; a long never also emits a short.
 * Callback runs synchronously; do not recursively update this state. */
void Buttons_Init(ButtonsState *state, uint8_t raw_mask, uint32_t now,
                  ButtonEventCallback callback, void *context);
void Buttons_Update(ButtonsState *state, uint8_t raw_mask, uint32_t now);
#endif
