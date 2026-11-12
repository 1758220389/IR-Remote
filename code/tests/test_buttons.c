#include "buttons_logic.h"
#include <assert.h>
#include <stdio.h>
static unsigned count;
static ButtonEvent events[100];
static void collect(void *context, const ButtonEvent *event)
{
    (void)context;
    assert(count < 100u);
    events[count++] = *event;
}
static void test_startup_held_is_blocked(void)
{
    ButtonsState b;
    count = 0u;
    Buttons_Init(&b, 31u, 0u, collect, 0);
    Buttons_Update(&b, 31u, 2000u);
    assert(b.blocked_mask == 31u);
    assert(count == 0u);
}
static void test_short_all_keys_on_release(void)
{
    ButtonsState b;
    unsigned k;
    uint32_t t;
    uint8_t mask;
    count = 0u;
    Buttons_Init(&b, 0u, 0u, collect, 0);
    for (k = 0u; k < 5u; ++k) {
        t = 100u + k * 200u;
        mask = (uint8_t)(1u << k);
        Buttons_Update(&b, mask, t);
        Buttons_Update(&b, mask, t + 19u);
        assert(count == k);
        Buttons_Update(&b, mask, t + 20u);
        assert(count == k);
        Buttons_Update(&b, 0u, t + 80u);
        Buttons_Update(&b, 0u, t + 99u);
        assert(count == k);
        Buttons_Update(&b, 0u, t + 100u);
        assert(count == k + 1u);
        assert(events[k].key == k + 1u);
        assert(events[k].type == BUTTON_SHORT);
    }
}

static void test_long_all_keys_once_no_release_click(void)
{
    ButtonsState b;
    unsigned k;
    uint32_t t;
    uint8_t mask;
    count = 0u;
    Buttons_Init(&b, 0u, 0u, collect, 0);
    for (k = 0u; k < 5u; ++k) {
        t = 100u + k * 3000u;
        mask = (uint8_t)(1u << k);
        Buttons_Update(&b, mask, t);
        Buttons_Update(&b, mask, t + 20u);
        Buttons_Update(&b, mask, t + 1019u);
        assert(count == k);
        Buttons_Update(&b, mask, t + 1020u);
        assert(count == k + 1u);
        assert(events[k].key == k + 1u);
        assert(events[k].type == BUTTON_LONG);
        Buttons_Update(&b, mask, t + 1500u);
        Buttons_Update(&b, 0u, t + 1501u);
        Buttons_Update(&b, 0u, t + 1521u);
        assert(count == k + 1u);
    }
}

static void test_release_at_long_threshold_is_not_short(void)
{
    ButtonsState b;
    count = 0u;
    Buttons_Init(&b, 0u, 0u, collect, 0);
    Buttons_Update(&b, 16u, 10u);
    Buttons_Update(&b, 16u, 30u);
    Buttons_Update(&b, 0u, 1030u);
    Buttons_Update(&b, 0u, 1050u);
    assert(count == 1u && events[0].type == BUTTON_LONG);
    assert(events[0].key == 5u);
}

static void test_bounce_both_edges_and_rollover(void)
{
    ButtonsState b;
    uint32_t t;
    t = UINT32_MAX - 50u;
    count = 0u;
    Buttons_Init(&b, 0u, t, collect, 0);
    Buttons_Update(&b, 1u, (uint32_t)(t + 1u));
    Buttons_Update(&b, 0u, (uint32_t)(t + 10u));
    Buttons_Update(&b, 1u, (uint32_t)(t + 15u));
    Buttons_Update(&b, 1u, (uint32_t)(t + 34u));
    assert(b.stable_mask == 0u);
    Buttons_Update(&b, 1u, (uint32_t)(t + 35u));
    assert(b.stable_mask == 1u);
    Buttons_Update(&b, 0u, (uint32_t)(t + 50u));
    Buttons_Update(&b, 1u, (uint32_t)(t + 55u));
    Buttons_Update(&b, 0u, (uint32_t)(t + 60u));
    Buttons_Update(&b, 0u, (uint32_t)(t + 79u));
    assert(count == 0u);
    Buttons_Update(&b, 0u, (uint32_t)(t + 80u));
    assert(count == 1u && events[0].type == BUTTON_SHORT);
    count = 0u;
    Buttons_Init(&b, 0u, t, collect, 0);
    Buttons_Update(&b, 31u, t);
    Buttons_Update(&b, 31u, (uint32_t)(t + 20u));
    Buttons_Update(&b, 31u, (uint32_t)(t + 1020u));
    assert(count == 5u);
    Buttons_Update(&b, 0u, (uint32_t)(t + 1100u));
    Buttons_Update(&b, 0u, (uint32_t)(t + 1120u));
    assert(count == 5u);
}

static void test_startup_release_requires_debounce_then_accepts_new_press(void)
{
    ButtonsState b;
    count = 0u;
    Buttons_Init(&b, 31u, 0u, collect, 0);
    Buttons_Update(&b, 0u, 10u);
    Buttons_Update(&b, 31u, 20u);
    Buttons_Update(&b, 31u, 2000u);
    assert(count == 0u && b.blocked_mask == 31u);
    Buttons_Update(&b, 0u, 2010u);
    Buttons_Update(&b, 0u, 2030u);
    assert(count == 0u && b.blocked_mask == 0u);
    Buttons_Update(&b, 31u, 2100u);
    Buttons_Update(&b, 31u, 2120u);
    Buttons_Update(&b, 0u, 2150u);
    Buttons_Update(&b, 0u, 2170u);
    assert(count == 5u);
    Buttons_Init(&b, 0u, 0u, 0, 0);
    Buttons_Update(&b, 1u, 10u);
    Buttons_Update(&b, 1u, 30u);
    Buttons_Update(&b, 0u, 50u);
    Buttons_Update(&b, 0u, 70u);
    assert(b.stable_mask == 0u);
}

static void test_release_just_before_long_threshold_stays_short(void)
{
    ButtonsState b;
    count = 0u;
    Buttons_Init(&b, 0u, 0u, collect, 0);
    Buttons_Update(&b, 16u, 10u);
    Buttons_Update(&b, 16u, 30u);
    Buttons_Update(&b, 0u, 1029u);
    Buttons_Update(&b, 0u, 1049u);
    assert(count == 1u && events[0].type == BUTTON_SHORT);
}

int main(void)
{
    test_release_just_before_long_threshold_stays_short();
    test_startup_release_requires_debounce_then_accepts_new_press();
    test_bounce_both_edges_and_rollover();
    test_release_at_long_threshold_is_not_short();
    test_long_all_keys_once_no_release_click();
    test_short_all_keys_on_release();
    test_startup_held_is_blocked();
    puts("buttons: all tests passed");
    return 0;
}
