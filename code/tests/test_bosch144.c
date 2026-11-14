/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Tests use the attributed upstream real capture in tests/fixtures/. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bosch144.h"
static UiSignal signal_a;
static UiSignal signal_b;
static uint16_t capture[299];
static uint16_t read_capture(void *context, uint16_t index) {
    return ((uint16_t *)context)[index];
}
static void test_real_fixture(const char *path) {
    FILE *file;
    int c;
    unsigned int value;
    unsigned int count;
    Bosch144State state;
    file = fopen(path, "r");
    assert(file != NULL);
    do { c = fgetc(file); } while (c != '[' && c != EOF);
    assert(c == '[');
    count = 0u;
    while (fscanf(file, "%u", &value) == 1) {
        assert(count < 299u && value <= 65535u);
        capture[count++] = (uint16_t)value;
        c = fgetc(file);
        if (c != ',') break;
    }
    fclose(file);
    assert(count == 299u);
    assert(Bosch144_Decode(read_capture, capture, 299u, &signal_a));
    assert(Bosch144_ValidateSignal(&signal_a));
    Bosch144_Init(&state);
    assert(Bosch144_StateFromSignal(&state, &signal_a));
    assert(state.power == 1u);
    assert(Bosch144_GetTempHalf(&state) == 32u);
    assert(Bosch144_GetMode(&state) == BOSCH144_MODE_COOL);
}
static void test_state_envelope(void) {
    Bosch144State state;
    Bosch144State decoded;
    unsigned int half;
    unsigned int i;
    static const uint8_t codes[15] = {2u,0u,4u,12u,8u,24u,28u,20u,16u,48u,52u,36u,32u,40u,44u};
    unsigned int code;
    Bosch144_Init(&state);
    assert(sizeof(state) == 19u);
    assert(Bosch144_GetTempHalf(&state) == 51u);
    assert(Bosch144_GetFan(&state) == BOSCH144_FAN_AUTO);
    state.raw[14] |= 0x89u;
    state.raw[15] |= 0xA0u;
    state.raw[16] = 0x42u;
    for (half = 32u; half <= 60u; ++half) {
        Bosch144_SetTempHalf(&state, (uint8_t)half);
        assert(Bosch144_GetTempHalf(&state) == half);
        code = (unsigned int)codes[(half - 32u) / 2u] | (half & 1u);
        assert((state.raw[4] >> 4) == (code >> 2));
        assert(((state.raw[14] >> 5) & 1u) == (code & 1u));
        assert(((state.raw[15] >> 4) & 1u) == ((code >> 1) & 1u));
        assert((state.raw[14] & 0xDFu) == 0x89u);
        assert((state.raw[15] & 0xEEu) == 0xA0u);
        assert(state.raw[16] == 0x42u);
        assert(Bosch144_Generate(&state, &signal_a));
        assert(Bosch144_ValidateSignal(&signal_a));
        assert(Bosch144_DurationCount(&signal_a) == 299u);
        for (i = 0u; i < 299u; ++i)
            capture[i] = Bosch144_Duration(&signal_a, (uint16_t)i);
        assert(Bosch144_Duration(&signal_a, 299u) == 0u);
        assert(Bosch144_Decode(read_capture, capture, 299u, &signal_b));
        Bosch144_Init(&decoded);
        assert(Bosch144_StateFromSignal(&decoded, &signal_b));
        assert(Bosch144_GetTempHalf(&decoded) == half);
        assert(memcmp(signal_a.timings, signal_b.timings, 36u) == 0);
    }
    Bosch144_SetPower(&state, 0u);
    assert(Bosch144_Generate(&state, &signal_a));
    assert(signal_a.length == 12u);
    assert(Bosch144_DurationCount(&signal_a) == 199u);
    for (i = 0u; i < 199u; ++i)
        capture[i] = Bosch144_Duration(&signal_a, (uint16_t)i);
    assert(Bosch144_Decode(read_capture, capture, 199u, &signal_b));
    assert(Bosch144_StateFromSignal(&decoded, &signal_b));
    assert(!decoded.power && Bosch144_GetTempHalf(&decoded) == 60u);
    Bosch144_SetPower(&decoded, 1u);
    assert(Bosch144_Generate(&decoded, &signal_a));
    assert(signal_a.length == 18u);
}
static void test_controls(void) {
    static const uint8_t modes[5] = {0u,3u,6u,2u,5u};
    static const uint16_t fans[6] = {458u,276u,158u,104u,114u,371u};
    Bosch144State state;
    Bosch144State saved;
    unsigned int i;
    Bosch144_Init(&state);
    assert(Bosch144_SetMode(&state, 5u));
    for (i = 0u; i < 5u; ++i) {
        assert(Bosch144_Button(&state, 3u));
        assert(Bosch144_GetMode(&state) == modes[i]);
        if (modes[i] == 3u || modes[i] == 5u)
            assert(Bosch144_GetFan(&state) == 51u);
    }
    assert(Bosch144_SetFan(&state, 371u));
    for (i = 0u; i < 6u; ++i) {
        assert(Bosch144_Button(&state, 4u));
        assert(Bosch144_GetFan(&state) == fans[i]);
    }
    assert(Bosch144_SetFan(&state, 51u));
    assert(Bosch144_Button(&state, 4u));
    assert(Bosch144_GetFan(&state) == 458u);
    saved = state;
    assert(!Bosch144_SetFan(&state, 999u));
    assert(!Bosch144_SetMode(&state, 7u));
    assert(!Bosch144_Button(&state, 0u));
    assert(memcmp(&saved, &state, sizeof(state)) == 0);
    Bosch144_SetTempHalf(&state, 0u);
    assert(Bosch144_GetTempHalf(&state) == 32u);
    assert(Bosch144_Button(&state, 2u));
    assert(Bosch144_GetTempHalf(&state) == 32u);
    assert(Bosch144_Button(&state, 1u));
    assert(Bosch144_GetTempHalf(&state) == 33u);
    Bosch144_SetTempHalf(&state, 255u);
    assert(Bosch144_Button(&state, 1u));
    assert(Bosch144_GetTempHalf(&state) == 60u);
    assert(Bosch144_Button(&state, 2u));
    assert(Bosch144_GetTempHalf(&state) == 59u);
    assert(Bosch144_Button(&state, 5u) && !state.power);
    assert(Bosch144_Button(&state, 5u) && state.power);
}
static void test_rejections(void) {
    Bosch144State state;
    unsigned int i;
    unsigned int saved;
    Bosch144_Init(&state);
    assert(Bosch144_Generate(&state, &signal_a));
    assert(signal_a.timings[17] == 0x5Bu);
    assert(signal_a.timings[0] == 0xB2u && signal_a.timings[13] == 0x66u);
    for (i = 0u; i < 299u; ++i)
        capture[i] = Bosch144_Duration(&signal_a, (uint16_t)i);
    signal_b = signal_a;
    assert(!Bosch144_Decode(read_capture, capture, 283u, &signal_b));
    assert(memcmp(&signal_a, &signal_b, sizeof(signal_a)) == 0);
    for (i = 0u; i < 299u; ++i) {
        saved = capture[i];
        capture[i] = 100u;
        assert(!Bosch144_Decode(read_capture, capture, 299u, &signal_b));
        capture[i] = (uint16_t)saved;
    }
    for (i = 0u; i < 18u; ++i) {
        signal_b = signal_a;
        signal_b.timings[i] ^= 1u;
        assert(!Bosch144_ValidateSignal(&signal_b));
        assert(!Bosch144_StateFromSignal(&state, &signal_b));
        assert(!Bosch144_DurationCount(&signal_b));
    }
    for (i = 2u; i < 299u; i += 2u) {
        if (i % 100u != 0u) capture[i] = (uint16_t)((i & 2u) ? 250u : 850u);
    }
    assert(Bosch144_Decode(read_capture, capture, 299u, &signal_b));
    /* Valid timings but corrupt checksum, not merely bad pulse widths. */
    capture[297] = capture[297] == 1645u ? 610u : 1645u;
    assert(!Bosch144_Decode(read_capture, capture, 299u, &signal_b));
    assert(!Bosch144_ValidateSignal(NULL));
    assert(!Bosch144_Decode(NULL, capture, 299u, &signal_b));
    assert(!Bosch144_Decode(read_capture, capture, 299u, NULL));
    signal_b = signal_a;
    signal_b.length = 256u;
    assert(!Bosch144_ValidateSignal(&signal_b));
    signal_b = signal_a;
    signal_b.timings[0] = 0x1B2u;
    assert(!Bosch144_ValidateSignal(&signal_b));
    signal_b = signal_a;
    signal_b.format = UI_SIGNAL_RAW;
    assert(!Bosch144_ValidateSignal(&signal_b));
    signal_b = signal_a;
    signal_b.carrier_khz = 0u;
    assert(!Bosch144_ValidateSignal(&signal_b));
}
int main(int argc, char **argv) {
    (void)signal_b;
    assert(argc == 2);
    test_real_fixture(argv[1]);
    test_state_envelope();
    test_controls();
    test_rejections();
    puts("bosch144: real299, 29 half-degree states, on299/off199, controls, rejection vectors passed");
    return 0;
}
