#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void test_init(void)
{
    UiState u;
    Ui_Init(&u, 0, 50u);
    assert(u.page == UI_PAGE_NORMAL);
    assert(u.active_profile == 0u);
    assert(u.dirty == 1u);
    assert(u.learned_mask == 0u);
    assert(u.profiles[0].bound_mask == 0u);
}
static unsigned sent;
static uint8_t sent_key, sent_profile;
static const UiSignal *sent_signal;
static void send_event(void *context, uint8_t profile, uint8_t key,
                       const UiSignal *signal)
{
    assert(context == &sent);
    ++sent; sent_key = key; sent_profile = profile; sent_signal = signal;
}
static void press(UiState *u, uint8_t key, ButtonEventType type, uint32_t now)
{
    ButtonEvent event;
    event.key = key; event.type = type;
    Ui_Button(u, &event, now);
}
static void test_normal_short_all_five_send_interface_event(void)
{
    UiState u;
    UiCallbacks cb;
    uint8_t key;
    memset(&cb, 0, sizeof(cb));
    cb.send = send_event; cb.context = &sent;
    Ui_Init(&u, &cb, 0u);
    sent = 0u;
    for (key = 1u; key <= 5u; ++key) {
        press(&u, key, BUTTON_SHORT, 10u);
        assert(sent == key);
        assert(sent_key == key && sent_profile == 0u);
        assert(sent_signal == 0);
        assert(u.page == UI_PAGE_NORMAL);
    }
}

static void test_settings_long_navigation_and_wrapping(void)
{
    UiState u;
    unsigned i;
    Ui_Init(&u, 0, 0u);
    for (i = 1u; i <= 4u; ++i) {
        press(&u, (uint8_t)i, BUTTON_LONG, 0u);
        assert(u.page == UI_PAGE_NORMAL);
    }
    press(&u, 5u, BUTTON_LONG, 1u);
    assert(u.page == UI_PAGE_SETTINGS && u.selection == 0u);
    u.dirty = 0u;
    press(&u, 1u, BUTTON_SHORT, 2u);
    assert(u.selection == 3u && u.dirty == 1u);
    for (i = 0u; i < 4u; ++i) press(&u, 2u, BUTTON_SHORT, 3u);
    assert(u.selection == 3u);
    press(&u, 2u, BUTTON_SHORT, 4u);
    assert(u.selection == 0u);
    press(&u, 5u, BUTTON_LONG, 5u);
    assert(u.page == UI_PAGE_NORMAL);
}

static void test_confirm_each_profile(void)
{
    UiState u;
    unsigned p, i;
    Ui_Init(&u, 0, 0u);
    for (p = 0u; p < 3u; ++p) {
        press(&u, 5u, BUTTON_LONG, 0u);
        for (i = 0u; i <= p; ++i) press(&u, 2u, BUTTON_SHORT, 0u);
        press(&u, 5u, BUTTON_SHORT, 0u);
        assert(u.page == UI_PAGE_NORMAL);
        assert(u.active_profile == p);
    }
}

static void enter_learning(UiState *u)
{
    press(u, 5u, BUTTON_LONG, 0u);
    press(u, 5u, BUTTON_SHORT, 0u);
}
static void test_empty_learning_exit_without_save(void)
{
    UiState u;
    Ui_Init(&u, 0, 0u);
    enter_learning(&u);
    assert(u.page == UI_PAGE_RECEIVING && u.learned_mask == 0u);
    press(&u, 1u, BUTTON_SHORT, 0u);
    assert(u.page == UI_PAGE_RECEIVING && u.learned_mask == 0u);
    press(&u, 5u, BUTTON_LONG, 0u);
    assert(u.page == UI_PAGE_NORMAL);
}

static UiSignal make_signal(uint16_t value)
{
    UiSignal signal;
    memset(&signal, 0, sizeof(signal));
    signal.length = 2u; signal.carrier_khz = 38u;
    signal.timings[0] = value; signal.timings[1] = 560u;
    return signal;
}
static void test_real_receive_enters_received_and_copies(void)
{
    UiState u;
    UiSignal signal;
    Ui_Init(&u, 0, 0u);
    signal = make_signal(9000u);
    assert(Ui_Receive(&u, &signal, 0u) == 0);
    enter_learning(&u);
    assert(Ui_Receive(&u, 0, 0u) == 0);
    signal.length = 0u;
    assert(Ui_Receive(&u, &signal, 0u) == 0);
    signal.length = UI_SIGNAL_MAX_TIMINGS + 1u;
    assert(Ui_Receive(&u, &signal, 0u) == 0);
    signal.length = 2u;
    u.dirty = 0u;
    assert(Ui_Receive(&u, &signal, 0u) == 1);
    assert(u.page == UI_PAGE_RECEIVED && u.dirty == 1u);
    signal.timings[0] = 7u;
    assert(u.captured.timings[0] == 9000u);
    assert(Ui_Receive(&u, &signal, 0u) == 0);
}

static void test_binding_all_keys_and_rebinding_overwrites(void)
{
    UiState u;
    UiSignal signal;
    uint8_t k;
    Ui_Init(&u, 0, 0u);
    enter_learning(&u);
    for (k = 1u; k <= 5u; ++k) {
        signal = make_signal((uint16_t)(k * 100u));
        assert(Ui_Receive(&u, &signal, 0u) == 1);
        press(&u, k, BUTTON_SHORT, 0u);
        assert(u.page == UI_PAGE_RECEIVING);
        assert(u.learned_mask == (1u << k) - 1u);
        assert(u.learning.bound_mask == u.learned_mask);
        assert(u.learning.keys[k - 1u].timings[0] == k * 100u);
    }
    signal = make_signal(777u);
    assert(Ui_Receive(&u, &signal, 0u) == 1);
    press(&u, 3u, BUTTON_SHORT, 0u);
    assert(u.learned_mask == 31u && u.learning.keys[2].timings[0] == 777u);
}

static void bind_one(UiState *u, uint8_t key, uint16_t value)
{
    UiSignal signal;
    signal = make_signal(value);
    assert(Ui_Receive(u, &signal, 0u) == 1);
    press(u, key, BUTTON_SHORT, 0u);
}
static void test_save_select_wrap_from_both_learning_pages(void)
{
    UiState u;
    UiSignal signal;
    unsigned i;
    Ui_Init(&u, 0, 0u);
    enter_learning(&u);
    bind_one(&u, 1u, 123u);
    press(&u, 5u, BUTTON_LONG, 10u);
    assert(u.page == UI_PAGE_SAVE_SELECT && u.save_profile == 0u);
    press(&u, 1u, BUTTON_SHORT, 10u);
    assert(u.save_profile == 2u);
    for (i = 0u; i < 3u; ++i) press(&u, 2u, BUTTON_SHORT, 10u);
    assert(u.save_profile == 2u);
    press(&u, 2u, BUTTON_SHORT, 10u);
    assert(u.save_profile == 0u);
    Ui_Init(&u, 0, 0u);
    enter_learning(&u);
    bind_one(&u, 1u, 123u);
    signal = make_signal(321u);
    assert(Ui_Receive(&u, &signal, 0u) == 1);
    press(&u, 5u, BUTTON_LONG, 0u);
    assert(u.page == UI_PAGE_SAVE_SELECT && u.learned_mask == 1u);
    assert(u.learning.keys[0].timings[0] == 123u);
}

static void prepare_save(UiState *u)
{
    enter_learning(u);
    bind_one(u, 2u, 456u);
    press(u, 5u, BUTTON_LONG, 0u);
}
static void test_ram_save_replaces_entire_destination(void)
{
    UiState u;
    UiSignal old;
    Ui_Init(&u, 0, 0u);
    old = make_signal(999u);
    u.profiles[1].bound_mask = 31u;
    u.profiles[1].keys[0] = old;
    u.profiles[2].bound_mask = 16u;
    prepare_save(&u);
    press(&u, 2u, BUTTON_SHORT, 0u);
    press(&u, 5u, BUTTON_LONG, 100u);
    assert(u.page == UI_PAGE_RESULT && u.result == UI_RESULT_SAVED_RAM);
    assert(u.profiles[1].bound_mask == 2u);
    assert(u.profiles[1].keys[0].length == 0u);
    assert(u.profiles[1].keys[1].timings[0] == 456u);
    assert(u.profiles[0].bound_mask == 0u && u.profiles[2].bound_mask == 16u);
    assert(u.active_profile == 0u);
}

static void test_cancel_all_four_keys_preserves_profiles(void)
{
    UiState u;
    uint8_t k;
    for (k = 1u; k <= 4u; ++k) {
        Ui_Init(&u, 0, 0u);
        u.profiles[0].bound_mask = 16u;
        prepare_save(&u);
        press(&u, k, BUTTON_LONG, 321u);
        assert(u.page == UI_PAGE_RESULT && u.result == UI_RESULT_CANCELLED);
        assert(u.profiles[0].bound_mask == 16u);
        assert(u.result_at == 321u);
    }
}

static unsigned save_calls;
static int save_ok;
static uint8_t saved_destination;
static UiProfile saved_profile;
static int save_backend(void *context, uint8_t destination, const UiProfile *profile)
{
    assert(context == &save_calls);
    ++save_calls; saved_destination = destination; saved_profile = *profile;
    return save_ok;
}
static void test_save_backend_failure_does_not_replace(void)
{
    UiState u;
    UiCallbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.save = save_backend; cb.context = &save_calls;
    save_calls = 0u; save_ok = 0;
    Ui_Init(&u, &cb, 0u);
    u.profiles[0].bound_mask = 16u;
    prepare_save(&u);
    press(&u, 5u, BUTTON_LONG, 100u);
    assert(save_calls == 1u && saved_destination == 0u);
    assert(saved_profile.bound_mask == 2u && saved_profile.keys[1].timings[0] == 456u);
    assert(u.result == UI_RESULT_SAVE_FAILED && u.page == UI_PAGE_RESULT);
    assert(u.profiles[0].bound_mask == 16u);
}

static void test_save_backend_success_commits_replacement(void)
{
    UiState u;
    UiCallbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.save = save_backend; cb.context = &save_calls;
    save_calls = 0u; save_ok = 1;
    Ui_Init(&u, &cb, 0u);
    u.profiles[0].bound_mask = 31u;
    prepare_save(&u);
    press(&u, 5u, BUTTON_LONG, 100u);
    assert(save_calls == 1u);
    assert(u.result == UI_RESULT_SAVED_FLASH && u.page == UI_PAGE_RESULT);
    assert(u.profiles[0].bound_mask == 2u && u.profiles[0].keys[0].length == 0u);
}

static void test_result_timeout_rollover_ignores_keys(void)
{
    UiState u;
    uint32_t start;
    start = UINT32_MAX - 500u;
    Ui_Init(&u, 0, 0u);
    prepare_save(&u);
    press(&u, 1u, BUTTON_LONG, start);
    press(&u, 5u, BUTTON_LONG, start);
    assert(u.page == UI_PAGE_RESULT && u.result == UI_RESULT_CANCELLED);
    u.dirty = 0u;
    Ui_Tick(&u, (uint32_t)(start + 999u));
    assert(u.page == UI_PAGE_RESULT);
    Ui_Tick(&u, (uint32_t)(start + 1000u));
    assert(u.page == UI_PAGE_NORMAL && u.dirty == 1u);
    assert(u.learned_mask == 0u && u.learning.bound_mask == 0u);
}

static void test_blink_500ms_rollover_and_dirty(void)
{
    UiState u;
    uint32_t start;
    start = UINT32_MAX - 100u;
    Ui_Init(&u, 0, start);
    enter_learning(&u);
    u.dirty = 0u;
    Ui_Tick(&u, (uint32_t)(start + 499u));
    assert(u.blink == 1u && u.dirty == 0u);
    Ui_Tick(&u, (uint32_t)(start + 500u));
    assert(u.blink == 0u && u.dirty == 1u);
    u.dirty = 0u;
    Ui_Tick(&u, (uint32_t)(start + 1500u));
    assert(u.blink == 0u && u.dirty == 0u);
    Ui_Tick(&u, (uint32_t)(start + 2000u));
    assert(u.blink == 1u && u.dirty == 1u);
}

typedef struct {
    UiState *ui;
    uint32_t now;
} Dispatch;
static void dispatch(void *context, const ButtonEvent *event)
{
    Dispatch *d;
    d = (Dispatch *)context;
    Ui_Button(d->ui, event, d->now);
}
static void sample(ButtonsState *b, Dispatch *d, uint8_t raw, uint32_t now)
{
    d->now = now;
    Buttons_Update(b, raw, now);
    Ui_Tick(d->ui, now);
}
static void test_button_ui_transition_no_false_short(void)
{
    UiState u;
    ButtonsState b;
    Dispatch d;
    UiCallbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.send = send_event; cb.context = &sent;
    sent = 0u;
    Ui_Init(&u, &cb, 0u);
    d.ui = &u; d.now = 0u;
    Buttons_Init(&b, 0u, 0u, dispatch, &d);
    sample(&b, &d, 16u, 10u);
    sample(&b, &d, 16u, 30u);
    sample(&b, &d, 16u, 1030u);
    assert(u.page == UI_PAGE_SETTINGS && sent == 0u);
    sample(&b, &d, 0u, 1040u);
    sample(&b, &d, 0u, 1060u);
    assert(u.page == UI_PAGE_SETTINGS);
    sample(&b, &d, 16u, 1100u);
    sample(&b, &d, 16u, 1120u);
    sample(&b, &d, 0u, 1150u);
    sample(&b, &d, 0u, 1170u);
    assert(u.page == UI_PAGE_RECEIVING);
    bind_one(&u, 5u, 123u);
    sample(&b, &d, 16u, 1200u);
    sample(&b, &d, 16u, 1220u);
    sample(&b, &d, 16u, 2220u);
    assert(u.page == UI_PAGE_SAVE_SELECT);
    sample(&b, &d, 0u, 2230u);
    sample(&b, &d, 0u, 2250u);
    assert(u.page == UI_PAGE_SAVE_SELECT);
    sample(&b, &d, 16u, 2300u);
    sample(&b, &d, 16u, 2320u);
    sample(&b, &d, 16u, 3320u);
    assert(u.page == UI_PAGE_RESULT);
    sample(&b, &d, 16u, 4320u);
    assert(u.page == UI_PAGE_NORMAL);
    sample(&b, &d, 0u, 4330u);
    sample(&b, &d, 0u, 4350u);
    assert(u.page == UI_PAGE_NORMAL && sent == 0u);
}

static void test_bound_send_and_invalid_events(void)
{
    UiState u;
    UiCallbacks cb;
    ButtonEvent bad;
    memset(&cb, 0, sizeof(cb));
    cb.send = send_event; cb.context = &sent;
    sent = 0u;
    Ui_Init(&u, &cb, 0u);
    u.active_profile = 2u;
    u.profiles[2].keys[4] = make_signal(123u);
    u.profiles[2].bound_mask = 16u;
    press(&u, 5u, BUTTON_SHORT, 0u);
    assert(sent == 1u && sent_key == 5u && sent_profile == 2u);
    assert(sent_signal == &u.profiles[2].keys[4]);
    assert(sent_signal->timings[0] == 123u);
    bad.key = 0u; bad.type = BUTTON_SHORT;
    Ui_Button(&u, &bad, 0u);
    bad.key = 6u;
    Ui_Button(&u, &bad, 0u);
    Ui_Button(&u, 0, 0u);
    assert(sent == 1u && u.page == UI_PAGE_NORMAL);
    press(&u, 5u, BUTTON_LONG, 0u);
    press(&u, 3u, BUTTON_SHORT, 0u);
    press(&u, 4u, BUTTON_SHORT, 0u);
    assert(u.selection == 0u);
    press(&u, 5u, BUTTON_SHORT, 0u);
    bind_one(&u, 1u, 123u);
    press(&u, 5u, BUTTON_LONG, 0u);
    assert(u.save_profile == 2u);
    press(&u, 5u, BUTTON_SHORT, 0u);
    assert(u.page == UI_PAGE_SAVE_SELECT && sent == 1u);
}

static void test_no_synthetic_receive_and_fresh_learning_session(void)
{
    UiState u;
    UiSignal signal;
    Ui_Init(&u, 0, 0u);
    enter_learning(&u);
    Ui_Tick(&u, 100000u);
    assert(u.page == UI_PAGE_RECEIVING && u.learned_mask == 0u);
    signal = make_signal(10u);
    signal.length = UI_SIGNAL_MAX_TIMINGS;
    signal.timings[UI_SIGNAL_MAX_TIMINGS - 1u] = 65535u;
    assert(Ui_Receive(&u, &signal, 100001u) == 1);
    assert(u.captured.timings[UI_SIGNAL_MAX_TIMINGS - 1u] == 65535u);
    press(&u, 5u, BUTTON_LONG, 100002u);
    assert(u.page == UI_PAGE_NORMAL && u.learned_mask == 0u);
    enter_learning(&u);
    bind_one(&u, 4u, 99u);
    press(&u, 5u, BUTTON_LONG, 0u);
    press(&u, 1u, BUTTON_LONG, 0u);
    Ui_Tick(&u, 1000u);
    enter_learning(&u);
    assert(u.learned_mask == 0u && u.learning.bound_mask == 0u);
    assert(u.learning.keys[3].length == 0u && u.captured.length == 0u);
}

static void test_long_one_to_four_never_send(void)
{
    UiState u;
    UiCallbacks cb;
    uint8_t key;
    memset(&cb, 0, sizeof(cb));
    cb.send = send_event; cb.context = &sent;
    sent = 0u;
    Ui_Init(&u, &cb, 0u);
    for (key = 1u; key <= 4u; ++key) press(&u, key, BUTTON_LONG, 0u);
    assert(sent == 0u && u.page == UI_PAGE_NORMAL);
}

int main(void)
{
    test_long_one_to_four_never_send();
    test_no_synthetic_receive_and_fresh_learning_session();
    test_bound_send_and_invalid_events();
    test_button_ui_transition_no_false_short();
    test_blink_500ms_rollover_and_dirty();
    test_result_timeout_rollover_ignores_keys();
    test_save_backend_success_commits_replacement();
    test_save_backend_failure_does_not_replace();
    test_cancel_all_four_keys_preserves_profiles();
    test_ram_save_replaces_entire_destination();
    test_save_select_wrap_from_both_learning_pages();
    test_binding_all_keys_and_rebinding_overwrites();
    test_real_receive_enters_received_and_copies();
    test_empty_learning_exit_without_save();
    test_confirm_each_profile();
    test_settings_long_navigation_and_wrapping();
    test_normal_short_all_five_send_interface_event();
    test_init();
    puts("ui: all tests passed");
    printf("native state sizes: UI=%lu bytes, buttons=%lu bytes\n",
           (unsigned long)sizeof(UiState), (unsigned long)sizeof(ButtonsState));
    return 0;
}
