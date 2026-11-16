/* Upstream captures: see third_party/HAIER-NOTICE.txt. LGPL-2.1-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ac_haier.h"
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint16_t wave[800];
static uint16_t read_wave(void *context, uint16_t index)
{
    return ((const uint16_t *)context)[index];
}
static unsigned int array(const char *text, const char *key, uint16_t *values)
{
    const char *p;
    char *end;
    unsigned int n;
    p = strstr(text, key);
    CHECK(p != NULL);
    p = strchr(p, '[') + 1;
    n = 0;
    while (*p != ']') {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == ']') break;
        CHECK(n < 800u);
        values[n++] = (uint16_t)strtoul(p, &end, 10);
        CHECK(end != p);
        p = end;
    }
    return n;
}
static UiSignal fixture(const char *name, uint16_t format)
{
    FILE *f;
    char path[200], text[12000];
    size_t size;
    unsigned int n, i;
    uint16_t bytes[800];
    UiSignal out;
    sprintf(path, "tests/fixtures/upstream_haier_%s.json", name);
    f = fopen(path, "rb");
    CHECK(f != NULL);
    size = fread(text, 1, sizeof(text) - 1u, f);
    fclose(f);
    text[size] = '\0';
    n = array(text, "\"durations_us\"", wave);
    memset(&out, 0xA7, sizeof(out));
    CHECK(AcHaier_Decode(read_wave, wave, (uint16_t)n, &out));
    CHECK(out.format == format && out.carrier_khz == 38u);
    CHECK(out.length == array(text, "\"payload\"", bytes));
    for (i = 0; i < out.length; i++) CHECK(out.timings[i] == bytes[i]);
    CHECK(AcHaier_Validate(&out));
    return out;
}
static void waveform_tests(UiSignal signal)
{
    unsigned int i, n;
    uint16_t saved;
    UiSignal decoded, sentinel, invalid;
    n = AcHaier_DurationCount(&signal);
    CHECK(n == signal.length * 16u + 5u);
    CHECK(AcHaier_Duration(&signal, 0) == 3000u);
    CHECK(AcHaier_Duration(&signal, 1) == 3000u);
    CHECK(AcHaier_Duration(&signal, 2) == 3000u);
    CHECK(AcHaier_Duration(&signal, 3) == 4300u);
    CHECK(AcHaier_Duration(&signal, (uint16_t)(n - 1u)) == 520u);
    CHECK(AcHaier_Duration(&signal, (uint16_t)n) == 0u);
    for (i = 0; i < n; i++) wave[i] = AcHaier_Duration(&signal, (uint16_t)i);
    CHECK(AcHaier_Decode(read_wave, wave, (uint16_t)n, &decoded));
    CHECK(!memcmp(signal.timings, decoded.timings, signal.length * sizeof(uint16_t)));
    memset(&sentinel, 0xCC, sizeof(sentinel));
    for (i = 0; i < n; i++) {
        decoded = sentinel;
        saved = wave[i];
        wave[i] = 50u;
        CHECK(!AcHaier_Decode(read_wave, wave, (uint16_t)n, &decoded));
        CHECK(!memcmp(&decoded, &sentinel, sizeof(decoded)));
        wave[i] = saved;
    }
    for (i = 0; i < n; i++) CHECK(!AcHaier_Decode(read_wave, wave, (uint16_t)i, &decoded));
    /* Valid-timing single-bit corruption must fail checksum. */
    wave[5] = wave[5] == 650u ? 1650u : 650u;
    CHECK(!AcHaier_Decode(read_wave, wave, (uint16_t)n, &decoded));
    invalid = signal;
    invalid.timings[signal.length - 1u] ^= 1u;
    CHECK(!AcHaier_Validate(&invalid));
    CHECK(AcHaier_DurationCount(&invalid) == 0u);
    invalid = signal; invalid.timings[2] = (uint16_t)(invalid.timings[2] + 256u);
    CHECK(!AcHaier_Validate(&invalid));
    invalid = signal; invalid.carrier_khz = 40u;
    CHECK(!AcHaier_Validate(&invalid));
    invalid = signal; invalid.format = UI_SIGNAL_HAIER160;
    CHECK(!AcHaier_Validate(&invalid));
    invalid.format = UI_SIGNAL_HAIER176;
    CHECK(!AcHaier_Validate(&invalid));
    CHECK(!AcHaier_Validate(NULL));
    CHECK(!AcHaier_Decode(NULL, wave, (uint16_t)n, &decoded));
    CHECK(!AcHaier_Decode(read_wave, wave, (uint16_t)n, NULL));
}
static void state_tests(UiSignal signal)
{
    AcState state, before, empty;
    AcStatus status;
    UiSignal encoded, off;
    unsigned int i, base;
    memset(&empty, 0, sizeof(empty));
    state = empty;
    base = signal.format == UI_SIGNAL_HAIER;
    CHECK(AcHaier_Import(&state, &signal) == AC_IMPORT_STATE);
    CHECK(AcHaier_GetStatus(&state, &status));
    CHECK(status.power == 1u && status.mode == AC_MODE_COOL);
    CHECK(status.temp_half == (base ? 32u : 34u));
    CHECK(status.fan == (base ? AC_FAN_LOW : AC_FAN_HIGH));
    CHECK(AcHaier_Encode(&state, &encoded));
    CHECK(!memcmp(signal.timings, encoded.timings, signal.length * sizeof(uint16_t)));
    before = state;
    CHECK(!AcHaier_Button(&state, 0));
    CHECK(!AcHaier_Button(&state, 6));
    CHECK(!memcmp(&state, &before, sizeof(state)));
    CHECK(AcHaier_Button(&state, 1));
    CHECK(AcHaier_GetStatus(&state, &status));
    CHECK(status.temp_half == (base ? 34u : 36u));
    CHECK((base ? state.raw[1] & 15u : state.raw[12] & 31u) == (base ? 6u : 0u));
    CHECK(AcHaier_Button(&state, 2));
    CHECK((base ? state.raw[1] & 15u : state.raw[12] & 31u) == (base ? 7u : 1u));
    for (i = 0; i < 5u; i++) {
        CHECK(AcHaier_Button(&state, 3));
        CHECK(AcHaier_GetStatus(&state, &status));
        CHECK(status.mode == (i + 2u) % 5u);
        CHECK((base ? state.raw[1] & 15u : state.raw[12] & 31u) == (base ? 2u : 6u));
    }
    for (i = 0; i < 4u; i++) {
        CHECK(AcHaier_Button(&state, 4));
        CHECK(AcHaier_GetStatus(&state, &status));
        CHECK(status.fan == ((base ? 1u : 3u) + i + 1u) % 4u);
        CHECK((base ? state.raw[1] & 15u : state.raw[12] & 31u) == (base ? 3u : 4u));
    }
    before = state;
    CHECK(AcHaier_Button(&state, 5));
    CHECK(AcHaier_GetStatus(&state, &status) && !status.power);
    CHECK(AcHaier_Encode(&state, &off) && AcHaier_Validate(&off));
    CHECK((base ? off.timings[1] & 15u : off.timings[4] & 64u) == 0u);
    CHECK((state.raw[1] & 240u) == (before.raw[1] & 240u));
    CHECK(AcHaier_Import(&state, &off) == AC_IMPORT_STATE);
    CHECK(AcHaier_Button(&state, 5));
    CHECK(AcHaier_Encode(&state, &encoded));
    CHECK((base ? encoded.timings[1] & 15u : encoded.timings[4] & 64u) == (base ? 1u : 64u));
    for (i = 0; i < signal.length; i++) {
        if (i == 1u || i == signal.length - 1u || i == (base ? 5u : 12u)) continue;
        CHECK(encoded.timings[i] == signal.timings[i]);
    }
    for (i = 0; i < 25u; i++) CHECK(AcHaier_Button(&state, 1));
    CHECK(AcHaier_GetStatus(&state, &status) && status.temp_half == 60u);
    for (i = 0; i < 25u; i++) CHECK(AcHaier_Button(&state, 2));
    CHECK(AcHaier_GetStatus(&state, &status) && status.temp_half == 32u);
    before = state;
    signal.timings[0] ^= 1u;
    CHECK(!AcHaier_Import(&state, &signal));
    CHECK(!memcmp(&state, &before, sizeof(state)));
    if (base) {
        CHECK(AcHaier_Import(&empty, &off) == AC_IMPORT_PARTIAL);
        CHECK(empty.length == 0u);
    }
    CHECK(!AcHaier_Encode(&empty, &encoded));
    CHECK(!AcHaier_GetStatus(&empty, &status));
    CHECK(!AcHaier_Button(&empty, 1));
}
static void checksum(UiSignal *signal)
{
    unsigned int i, sum;
    sum = 0;
    for (i = 0; i + 1u < signal->length; i++) sum += signal->timings[i];
    signal->timings[signal->length - 1u] = (uint16_t)(sum & 255u);
}
static void semantic_tests(void)
{
    UiSignal base, yrw, bad, encoded;
    AcState state, before;
    AcStatus status, unchanged;
    base = fixture("72_on", UI_SIGNAL_HAIER);
    yrw = fixture("112", UI_SIGNAL_HAIER_YRW02);
    bad = base; bad.timings[2] &= 223u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    bad = base; bad.timings[1] = 0xF1u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    bad = base; bad.timings[1] = 0x04u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    bad = base; bad.timings[6] = 0xA0u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    bad = yrw; bad.timings[10] |= 32u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    bad = yrw; bad.timings[5] = 0u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    bad = yrw; bad.timings[7] = 0x60u; checksum(&bad);
    CHECK(!AcHaier_Validate(&bad));
    memset(&state, 0, sizeof(state));
    CHECK(AcHaier_Import(&state, &yrw) == AC_IMPORT_STATE);
    /* OFF from a different capture must select the power command even if the
     * remembered ON frame's last key was Fan. Preserve ON settings. */
    CHECK(AcHaier_Button(&state, 4));
    before = state;
    bad = yrw; bad.timings[4] &= 191u; bad.timings[12] = 5u; checksum(&bad);
    CHECK(AcHaier_Import(&state, &bad) == AC_IMPORT_STATE);
    CHECK(AcHaier_Encode(&state, &encoded));
    CHECK((encoded.timings[12] & 31u) == 5u);
    CHECK(state.raw[5] == before.raw[5]);
    memset(&state, 0, sizeof(state));
    bad = fixture("72_temp", UI_SIGNAL_HAIER);
    before = state;
    CHECK(AcHaier_Import(&state, &bad) == AC_IMPORT_PARTIAL);
    CHECK(!memcmp(&state, &before, sizeof(state)));
    CHECK(AcHaier_Import(&state, &base) == AC_IMPORT_STATE);
    before = state;
    state.raw[2] &= 223u;
    state.raw[8] = (uint8_t)(state.raw[8] - 32u);
    CHECK(!AcHaier_Encode(&state, &encoded));
    state = before;
    CHECK(AcHaier_Import(&state, &bad) == AC_IMPORT_STATE);
    CHECK(AcHaier_GetStatus(&state, &status) && status.temp_half == 44u);
    /* Corrupt state/status output is transactional. */
    memset(&status, 0xCC, sizeof(status)); unchanged = status;
    state.length = 255u;
    CHECK(!AcHaier_GetStatus(&state, &status));
    CHECK(!memcmp(&status, &unchanged, sizeof(status)));
    CHECK(!AcHaier_Import(NULL, &base));
    CHECK(!AcHaier_GetStatus(NULL, &status));
    CHECK(!AcHaier_Button(NULL, 1));
    CHECK(!AcHaier_Encode(NULL, &encoded));
}
int main(void)
{
    UiSignal signal;
    signal = fixture("72_on", UI_SIGNAL_HAIER);
    signal = fixture("72_temp", UI_SIGNAL_HAIER);
    signal = fixture("72_health", UI_SIGNAL_HAIER);
    signal = fixture("112", UI_SIGNAL_HAIER_YRW02);
    CHECK(signal.length == 14u);
    waveform_tests(fixture("72_on", UI_SIGNAL_HAIER));
    waveform_tests(signal);
    state_tests(fixture("72_on", UI_SIGNAL_HAIER));
    state_tests(fixture("112", UI_SIGNAL_HAIER_YRW02));
    semantic_tests();
    puts("Haier capture, waveform and state tests passed");
    return 0;
}
