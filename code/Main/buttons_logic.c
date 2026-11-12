#include "buttons_logic.h"
#include <string.h>
void Buttons_Init(ButtonsState *s, uint8_t raw, uint32_t now,
                  ButtonEventCallback callback, void *context)
{
    unsigned i;
    memset(s, 0, sizeof(*s));
    s->raw_mask = (uint8_t)(raw & BUTTON_MASK);
    s->stable_mask = s->raw_mask;
    s->blocked_mask = s->raw_mask;
    for (i = 0u; i < BUTTON_COUNT; ++i) {
        s->changed_at[i] = now;
        s->pressed_at[i] = now;
    }
    s->callback = callback; s->context = context;
}
void Buttons_Update(ButtonsState *s, uint8_t raw, uint32_t now)
{
    unsigned i;
    uint8_t bit;
    ButtonEvent event;
    raw = (uint8_t)(raw & BUTTON_MASK);
    for (i = 0u; i < BUTTON_COUNT; ++i) {
        bit = (uint8_t)(1u << i);
        if ((raw & bit) != (s->raw_mask & bit)) {
            s->raw_mask ^= bit;
            s->changed_at[i] = now;
        }
        if ((raw & bit) != (s->stable_mask & bit) &&
            (uint32_t)(now - s->changed_at[i]) >= BUTTON_DEBOUNCE_MS) {
            s->stable_mask ^= bit;
            if ((raw & bit) != 0u) {
                s->pressed_at[i] = now;
                s->long_mask &= (uint8_t)~bit;
            } else if ((s->blocked_mask & bit) != 0u) {
                s->blocked_mask &= (uint8_t)~bit;
            } else if ((s->long_mask & bit) == 0u) {
                event.key = (uint8_t)(i + 1u);
                event.type = (uint32_t)(s->changed_at[i] - s->pressed_at[i]) >= BUTTON_LONG_MS
                             ? BUTTON_LONG : BUTTON_SHORT;
                if (s->callback != 0) s->callback(s->context, &event);
            }
        }
        if ((s->stable_mask & bit) != 0u && (raw & bit) != 0u &&
            ((s->blocked_mask | s->long_mask) & bit) == 0u &&
            (uint32_t)(now - s->pressed_at[i]) >= BUTTON_LONG_MS) {
            s->long_mask |= bit;
            event.key = (uint8_t)(i + 1u);
            event.type = BUTTON_LONG;
            if (s->callback != 0) s->callback(s->context, &event);
        }
    }
}
