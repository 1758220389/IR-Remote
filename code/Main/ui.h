#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "buttons_logic.h"
#define UI_PROFILE_COUNT 3u
#define UI_KEY_COUNT 5u
#define UI_SIGNAL_MAX_TIMINGS 128u
#define UI_BLINK_MS 500u
#define UI_RESULT_MS 1000u
typedef struct {
    uint16_t length;
    uint16_t carrier_khz;
    uint16_t timings[UI_SIGNAL_MAX_TIMINGS];
} UiSignal;
typedef struct {
    UiSignal keys[UI_KEY_COUNT];
    uint8_t bound_mask;
} UiProfile;
typedef enum {
    UI_PAGE_NORMAL = 0, UI_PAGE_SETTINGS, UI_PAGE_RECEIVING,
    UI_PAGE_RECEIVED, UI_PAGE_SAVE_SELECT, UI_PAGE_RESULT
} UiPage;
typedef enum {
    UI_RESULT_NONE = 0, UI_RESULT_SAVED_RAM, UI_RESULT_SAVED_FLASH,
    UI_RESULT_CANCELLED, UI_RESULT_SAVE_FAILED
} UiResult;
/* All callbacks are synchronous, foreground-only and non-reentrant.
 * send is an interface event, including unbound keys (signal == NULL).
 * save: nonzero means durable storage succeeded; zero means failed.
 * NULL save means RAM-only, NEVER a claim of Flash persistence.
 * profile indices are 0..2, key numbers are 1..5. */
typedef void (*UiSendCallback)(void *context, uint8_t profile, uint8_t key,
                               const UiSignal *signal);
typedef int (*UiSaveCallback)(void *context, uint8_t destination,
                              const UiProfile *replacement);
typedef struct {
    UiSendCallback send;
    UiSaveCallback save;
    void *context;
} UiCallbacks;
typedef struct {
    UiPage page;
    uint8_t active_profile;
    uint8_t selection; /* Settings: 0=Learning, 1..3=Config1..3. */
    uint8_t save_profile;
    uint8_t learned_mask;
    uint8_t dirty; /* Renderer clears after drawing. */
    uint8_t blink;
    UiResult result;
    UiProfile profiles[UI_PROFILE_COUNT];
    UiProfile learning;
    UiSignal captured;
    UiCallbacks callbacks;
    uint32_t blink_at;
    uint32_t result_at;
} UiState;
/* Long keys 1..4 do not send. Long key 5 navigates; the button
 * processor suppresses its short event on release. No synthetic receive.
 * Init creates empty RAM profiles; application may restore profiles after
 * init. Call Receive only in foreground with a real decoded/captured signal.
 * Invalid signals and signals outside receiving are rejected (returns 0).
 * Timing units are defined by the future IR backend, not interpreted here.
 * Timestamp subtraction supports uint32_t wrap; tick at least every 2^31 ms. */
void Ui_Init(UiState *state, const UiCallbacks *callbacks, uint32_t now);
void Ui_Button(UiState *state, const ButtonEvent *event, uint32_t now);
int Ui_Receive(UiState *state, const UiSignal *signal, uint32_t now);
void Ui_Tick(UiState *state, uint32_t now);
#endif
