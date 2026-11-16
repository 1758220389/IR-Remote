#include "ui.h"
#include "ac_codec.h"
#include <string.h>
void Ui_Init(UiState *s, const UiCallbacks *callbacks, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    s->page = UI_PAGE_NORMAL;
    s->dirty = 1u;
    s->blink = 1u;
    s->blink_at = now;
    if (callbacks != 0) s->callbacks = *callbacks;
}
void Ui_Button(UiState *s, const ButtonEvent *event, uint32_t now)
{
    uint8_t key;
    const UiSignal *signal;
    if (event == 0 || event->key < 1u || event->key > UI_KEY_COUNT) return;
    key = event->key;
    if (s->page == UI_PAGE_NORMAL && event->type == BUTTON_LONG && key == 5u) {
        s->page = UI_PAGE_SETTINGS;
        s->selection = 0u;
        s->dirty = 1u;
    } else if (s->page == UI_PAGE_SETTINGS) {
        if (event->type == BUTTON_LONG && key == 5u) {
            s->page = UI_PAGE_NORMAL;
            s->dirty = 1u;
        } else if (event->type == BUTTON_SHORT && (key == 1u || key == 2u)) {
            s->selection = (uint8_t)((s->selection + (key == 1u ? 3u : 1u)) % 4u);
            s->dirty = 1u;
        } else if (event->type == BUTTON_SHORT && key == 5u) {
            if (s->selection == 0u) {
                memset(&s->learning, 0, sizeof(s->learning));
                memset(&s->captured, 0, sizeof(s->captured));
                s->learned_mask = 0u;
                s->receive_error = 0u;
                s->learn_ac = 1u;
                s->page = UI_PAGE_DEVICE_TYPE;
            } else {
                s->active_profile = (uint8_t)(s->selection - 1u);
                s->page = UI_PAGE_NORMAL;
            }
            s->dirty = 1u;
        }
    } else if (s->page == UI_PAGE_DEVICE_TYPE) {
        if (event->type == BUTTON_SHORT && (key == 1u || key == 2u)) {
            s->learn_ac ^= 1u;
            s->dirty = 1u;
        } else if (event->type == BUTTON_SHORT && key == 5u) {
            s->page = UI_PAGE_RECEIVING;
            s->dirty = 1u;
        } else if (event->type == BUTTON_LONG && key == 5u) {
            s->page = UI_PAGE_SETTINGS;
            s->selection = 0u;
            s->dirty = 1u;
        }
    } else if (s->page == UI_PAGE_AC_CONFIRM) {
        if (event->type == BUTTON_SHORT && key == 5u) {
            if (Ac_ProfileFromSignal(&s->learning,&s->captured)) {
                s->learned_mask = 31u;
                s->save_profile = s->active_profile;
                s->page = UI_PAGE_SAVE_SELECT;
                s->dirty = 1u;
            }
        } else if (event->type == BUTTON_SHORT && key == 1u) {
            s->captured.length = 0u;
            s->page = UI_PAGE_RECEIVING;
            s->dirty = 1u;
        } else if (event->type == BUTTON_LONG && key == 5u) {
            s->page = UI_PAGE_DEVICE_TYPE;
            s->dirty = 1u;
        }
    } else if (s->page == UI_PAGE_AC_UNSUPPORTED) {
        if (event->type == BUTTON_SHORT && key == 5u) {
            s->captured.length = 0u;
            s->receive_error = 0u;
            s->page = UI_PAGE_RECEIVING;
            s->dirty = 1u;
        } else if (event->type == BUTTON_SHORT && key == 4u) {
            s->learn_ac = 0u;
            s->receive_error = 0u;
            /* If the unknown frame exceeded RAW capacity, ask to receive a
             * new ordinary signal; never bind a truncated prefix. */
            s->page = s->captured.length ? UI_PAGE_RECEIVED : UI_PAGE_RECEIVING;
            s->dirty = 1u;
        } else if (event->type == BUTTON_LONG && key == 5u) {
            s->page = UI_PAGE_NORMAL;
            s->dirty = 1u;
        }
    } else if (s->page == UI_PAGE_RECEIVING && s->receive_error &&
               event->type == BUTTON_SHORT && key == 5u) {
        s->receive_error = 0u;
        s->dirty = 1u;
    } else if (s->page == UI_PAGE_RECEIVED && event->type == BUTTON_SHORT) {
        s->learning.keys[key - 1u] = s->captured;
        s->learned_mask |= (uint8_t)(1u << (key - 1u));
        s->learning.bound_mask = s->learned_mask;
        s->page = UI_PAGE_RECEIVING;
        s->dirty = 1u;
    } else if ((s->page == UI_PAGE_RECEIVING || s->page == UI_PAGE_RECEIVED) &&
               event->type == BUTTON_LONG && key == 5u) {
        if (s->learned_mask != 0u) {
            s->page = UI_PAGE_SAVE_SELECT;
            s->save_profile = s->active_profile;
        } else {
            s->page = UI_PAGE_NORMAL;
        }
        s->dirty = 1u;
    } else if (s->page == UI_PAGE_SAVE_SELECT) {
        if (event->type == BUTTON_SHORT && (key == 1u || key == 2u)) {
            s->save_profile = (uint8_t)((s->save_profile + (key == 1u ? 2u : 1u)) % 3u);
            s->dirty = 1u;
        } else if (event->type == BUTTON_LONG && key == 5u) {
            if (s->callbacks.save != 0) {
                if (s->callbacks.save(s->callbacks.context, s->save_profile, &s->learning)) {
                    s->profiles[s->save_profile] = s->learning;
                    s->result = UI_RESULT_SAVED_FLASH;
                } else {
                    s->result = UI_RESULT_SAVE_FAILED;
                }
            } else {
                s->profiles[s->save_profile] = s->learning;
                s->result = UI_RESULT_SAVED_RAM;
            }
            s->page = UI_PAGE_RESULT;
            s->result_at = now;
            s->dirty = 1u;
        } else if (event->type == BUTTON_LONG && key <= 4u) {
            s->result = UI_RESULT_CANCELLED;
            s->page = UI_PAGE_RESULT;
            s->result_at = now;
            s->dirty = 1u;
        }
    } else if (s->page == UI_PAGE_NORMAL && event->type == BUTTON_SHORT) {
        signal = 0;
        if ((s->profiles[s->active_profile].bound_mask & (1u << (key - 1u))) != 0u)
            signal = &s->profiles[s->active_profile].keys[key - 1u];
        if (s->callbacks.send != 0)
            s->callbacks.send(s->callbacks.context, s->active_profile, key, signal);
    }
}
int Ui_Receive(UiState *s, const UiSignal *signal, uint32_t now)
{
    AcState state;
    AcStatus status;
    (void)now;
    if (s->page != UI_PAGE_RECEIVING || signal == 0 ||
        signal->length == 0u || signal->length > UI_SIGNAL_MAX_TIMINGS ||
        signal->format > UI_SIGNAL_LAST ||
        (signal->format != UI_SIGNAL_RAW && !Ac_Validate(signal))) return 0;
    s->captured = *signal;
    s->receive_error = 0u;
    s->page = UI_PAGE_RECEIVED;
    if (s->learn_ac) {
        if (signal->format == UI_SIGNAL_RAW) {
            s->page = UI_PAGE_AC_UNSUPPORTED;
        } else if (Ac_Import(&state,signal) != AC_IMPORT_STATE || !state.power ||
                   !Ac_GetStatus(&state,&status)) {
            s->page = UI_PAGE_RECEIVING;
            s->receive_error = 2u; /* Need a complete powered-ON state. */
        } else {
            s->page = UI_PAGE_AC_CONFIRM;
        }
    }
    s->dirty = 1u;
    return 1;
}
void Ui_ReceiveError(UiState *s, uint8_t reason)
{
    if (!s || s->page != UI_PAGE_RECEIVING) return;
    s->captured.length = 0u;
    if (reason == UI_RX_UNSUPPORTED && s->learn_ac) {
        s->receive_error = 0u;
        s->page = UI_PAGE_AC_UNSUPPORTED;
    } else {
        s->receive_error = reason ? reason : UI_RX_INVALID;
    }
    s->dirty = 1u;
}
void Ui_Tick(UiState *s, uint32_t now)
{
    uint32_t periods;
    periods = (uint32_t)(now - s->blink_at) / UI_BLINK_MS;
    if (periods != 0u) {
        s->blink_at += periods * UI_BLINK_MS;
        if ((periods & 1u) != 0u) {
            s->blink ^= 1u;
            if (s->page == UI_PAGE_SETTINGS || s->page == UI_PAGE_SAVE_SELECT ||
                s->page == UI_PAGE_RECEIVING || s->page == UI_PAGE_RECEIVED)
                s->dirty = 1u;
        }
    }
    if (s->page == UI_PAGE_RESULT &&
        (uint32_t)(now - s->result_at) >= UI_RESULT_MS) {
        s->page = UI_PAGE_NORMAL;
        s->learned_mask = 0u;
        memset(&s->learning, 0, sizeof(s->learning));
        memset(&s->captured, 0, sizeof(s->captured));
        s->dirty = 1u;
    }
}
