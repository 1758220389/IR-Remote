#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ac_midea.h"
/* Published capture: see tests/fixtures/upstream_midea_real.json. */
static const uint16_t midea_real[] = {
    4366, 4470, 498, 1658, 522, 554, 498, 1658, 496, 580, 498, 580,
    498, 578, 498, 580, 498, 1658, 498, 1658, 498, 578, 498, 578,
    498, 580, 496, 582, 496, 578, 498, 1658, 498, 580, 498, 580,
    498, 1656, 498, 1656, 500, 580, 498, 578, 502, 576, 500, 1656,
    498, 1656, 500, 1654, 500, 1656, 500, 1656, 498, 1658, 498, 1656,
    500, 1658, 498, 1656, 498, 1656, 500, 1656, 500, 1654, 500, 1578,
    578, 1658, 498, 1656, 500, 1658, 498, 1656, 498, 1656, 500, 578,
    498, 1638, 516, 1656, 500, 578, 500, 1656, 500, 1656, 498, 1658,
    522, 554, 500, 5258, 4366, 4472, 498, 580, 498, 1658, 498, 580,
    498, 1656, 500, 1600, 556, 1658, 500, 1656, 500, 578, 498, 578,
    522, 1634, 498, 1588, 568, 1658, 498, 1656, 500, 1654, 498, 580,
    498, 1658, 498, 1658, 498, 580, 496, 578, 500, 1654, 500, 1636,
    518, 1656, 500, 578, 520, 558, 498, 578, 498, 580, 498, 576,
    500, 578, 498, 580, 498, 578, 498, 578, 498, 580, 498, 578,
    498, 580, 498, 580, 520, 556, 498, 580, 496, 580, 498, 578,
    500, 578, 498, 1658, 498, 580, 498, 578, 498, 1656, 500, 578,
    498, 580, 498, 580, 498, 1656, 522
};
static uint16_t rd(void *p, uint16_t i) { return ((const uint16_t *)p)[i]; }
static const uint8_t MODES[5] = {AC_MODE_DRY,AC_MODE_HEAT,AC_MODE_FAN,AC_MODE_AUTO,AC_MODE_COOL};
static const uint8_t FANS[4] = {AC_FAN_MEDIUM,AC_FAN_HIGH,AC_FAN_AUTO,AC_FAN_LOW};
static void dynamic_controls(void) {
    static const uint8_t native[] = {0xA1u,0x88u,0x40u,0xFFu,0xFFu,0x56u};
    UiSignal s, out;
    AcState state, saved;
    AcStatus status;
    unsigned int i;
    memset(&s, 0, sizeof(s));
    s.format = UI_SIGNAL_MIDEA; s.length = 6u; s.carrier_khz = 38u;
    for (i = 0u; i < 6u; ++i) s.timings[i] = native[i];
    assert(AcMidea_Import(&state, &s) == AC_IMPORT_STATE);
    assert(AcMidea_GetStatus(&state, &status));
    assert(status.power == 1u && status.temp_half == 34u);
    assert(AcMidea_Encode(&state, &out));
    for (i = 0u; i < 6u; ++i) assert(out.timings[i] == native[i]);
    assert(AcMidea_Button(&state, 1u));
    assert(AcMidea_GetStatus(&state, &status) && status.temp_half == 36u);
    assert(AcMidea_Button(&state, 2u));
    assert(AcMidea_GetStatus(&state, &status) && status.temp_half == 34u);
    for (i = 0u; i < 40u; ++i) assert(AcMidea_Button(&state, 1u));
    assert(AcMidea_GetStatus(&state, &status) && status.temp_half == 60u);
    for (i = 0u; i < 40u; ++i) assert(AcMidea_Button(&state, 2u));
    assert(AcMidea_GetStatus(&state, &status) && status.temp_half == 34u);
    for (i = 0u; i < 5u; ++i) {
        assert(AcMidea_Button(&state, 3u));
        assert(AcMidea_GetStatus(&state, &status));
        assert(status.mode == MODES[i]);
        assert(AcMidea_Encode(&state, &out) && AcMidea_Validate(&out));
    }
    /* Cycle from auto through all four canonical fan choices. */
    for (i = 0u; i < 4u; ++i) {
        assert(AcMidea_Button(&state, 4u));
        assert(AcMidea_GetStatus(&state, &status));
        assert(status.fan == FANS[i]);
    }
    saved = state;
    assert(!AcMidea_Button(&state, 0u));
    assert(!AcMidea_Button(&state, 6u));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(AcMidea_Button(&state, 5u));
    assert(AcMidea_GetStatus(&state, &status) && !status.power);
    assert(AcMidea_Encode(&state, &out) && AcMidea_Validate(&out));
    assert(!(out.timings[1] & 128u));
    assert(AcMidea_Button(&state, 5u));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    state.length = 0u; saved = state;
    assert(!AcMidea_GetStatus(&state, &status));
    assert(!AcMidea_Button(&state, 1u));
    assert(!AcMidea_Encode(&state, &out));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
}

static uint16_t stream_read(void *p, uint16_t i) {
    return AcMidea_Duration((const UiSignal *)p, i);
}
static void streaming_roundtrip(void) {
    static const uint8_t payload[] = {0xA1u,0x88u,0x40u,0xFFu,0xFFu,0x56u};
    UiSignal s, decoded, unchanged;
    uint16_t wave[401];
    unsigned int i, j, byte, bit, frame;
    uint8_t value;
    memset(&s, 0, sizeof(s));
    s.format = UI_SIGNAL_MIDEA; s.length = 6u; s.carrier_khz = 38u;
    for (i = 0u; i < 6u; ++i) s.timings[i] = payload[i];
    assert(AcMidea_DurationCount(&s) == 200u);
    for (i = 0u; i < 200u; ++i) {
        wave[i] = AcMidea_Duration(&s, (uint16_t)i);
        assert(wave[i] != 0u);
    }
    assert(AcMidea_Duration(&s, 200u) == 0u);
    assert(AcMidea_Duration(&s, 65535u) == 0u);
    for (frame = 0u; frame < 2u; ++frame) {
        j = frame * 100u;
        assert(wave[j++] == 4480u && wave[j++] == 4480u);
        for (byte = 0u; byte < 6u; ++byte) {
            value = payload[byte]; if (frame) value ^= 255u;
            for (bit = 0u; bit < 8u; ++bit) {
                assert(wave[j++] == 560u);
                assert(wave[j++] == ((value & (128u >> bit)) ? 1680u : 560u));
            }
        }
        assert(wave[j++] == 560u);
        assert(wave[j] == (frame ? 65535u : 5600u));
    }
    assert(AcMidea_Decode(stream_read, &s, 200u, &decoded));
    for (i = 0u; i < 6u; ++i) assert(s.timings[i] == decoded.timings[i]);
    assert(AcMidea_Decode(rd, wave, 199u, &decoded));
    /* Header, data marks/spaces, footer, and separator corruption. */
    memset(&unchanged, 0xA5, sizeof(unchanged));
    for (i = 0u; i < 199u; ++i) {
        uint16_t old;
        old = wave[i]; wave[i] = 1u; decoded = unchanged;
        assert(!AcMidea_Decode(rd, wave, 199u, &decoded));
        assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
        wave[i] = old;
    }
    /* Valid-looking data corruption must fail inverted-copy integrity. */
    wave[3] = wave[3] == 1680u ? 560u : 1680u;
    decoded = unchanged;
    assert(!AcMidea_Decode(rd, wave, 200u, &decoded));
    assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
    wave[3] = wave[3] == 1680u ? 560u : 1680u;
    /* Every non-envelope truncation is rejected; one Coolix frame is legal. */
    for (i = 0u; i < 199u; ++i) {
        if (0) continue;
        decoded = unchanged;
        assert(!AcMidea_Decode(rd, wave, (uint16_t)i, &decoded));
        assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
    }
    for (i = 0u; i < 200u; ++i) wave[i + 200u] = wave[i];
    assert(AcMidea_Decode(rd, wave, 400u, &decoded));
    assert(AcMidea_Decode(rd, wave, 399u, &decoded));
    wave[400] = 560u;
    assert(!AcMidea_Decode(rd, wave, 401u, &decoded));
    assert(!AcMidea_Decode(0, 0, 200u, &decoded));
    assert(!AcMidea_Decode(rd, wave, 200u, 0));
    s.timings[0] = 0u;
    assert(!AcMidea_DurationCount(&s));
    assert(!AcMidea_Duration(&s, 0u));
    assert(!AcMidea_DurationCount(0));
    assert(!AcMidea_Duration(0, 0u));
}

static void rejected_signal(const UiSignal *s) {
    AcState state, saved;
    UiSignal output, original;
    AcStatus status, prior;
    memset(&state, 0xA5, sizeof(state)); saved = state;
    assert(!AcMidea_Validate(s));
    assert(!AcMidea_Import(&state, s));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(!AcMidea_DurationCount(s));
    assert(!AcMidea_Duration(s, 0u));
    memset(&output, 0xA5, sizeof(output)); original = output;
    assert(!AcMidea_Encode(&state, &output));
    assert(memcmp(&output, &original, sizeof(output)) == 0);
    memset(&status, 0xA5, sizeof(status)); prior = status;
    assert(!AcMidea_GetStatus(&state, &status));
    assert(memcmp(&status, &prior, sizeof(status)) == 0);
}
static void partial_signal(UiSignal *s) {
    AcState state, saved;
    UiSignal decoded;
    memset(&state, 0xA5, sizeof(state)); saved = state;
    assert(AcMidea_Validate(s));
    assert(AcMidea_Import(&state, s) == AC_IMPORT_PARTIAL);
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(AcMidea_Decode(stream_read, s, AcMidea_DurationCount(s), &decoded));
}
static void null_arguments(void) {
    AcState state;
    UiSignal s;
    AcStatus status;
    memset(&state, 0, sizeof(state));
    memset(&s, 0, sizeof(s));
    assert(!AcMidea_Validate(0));
    assert(!AcMidea_Import(0, &s));
    assert(!AcMidea_Import(&state, 0));
    assert(!AcMidea_GetStatus(0, &status));
    assert(!AcMidea_GetStatus(&state, 0));
    assert(!AcMidea_Button(0, 1u));
    assert(!AcMidea_Encode(0, &s));
    assert(!AcMidea_Encode(&state, 0));
}

/* Independent checksum oracle: reverse bits, add modulo 256, negate. */
static unsigned int reversed(unsigned int x) {
    unsigned int r, i;
    r = 0u;
    for (i = 0u; i < 8u; ++i) r |= ((x >> i) & 1u) << (7u - i);
    return r;
}
static void repair(UiSignal *s) {
    unsigned int i, sum;
    sum = 0u;
    for (i = 0u; i < 5u; ++i) sum += reversed(s->timings[i]);
    s->timings[5] = (uint16_t)reversed((256u - (sum & 255u)) & 255u);
}
static void validation_and_preservation(void) {
    UiSignal s, valid, out;
    AcState state, saved;
    AcStatus status;
    uint16_t wave[200];
    unsigned int i, bit, key;
    assert(AcMidea_Decode(rd, (void *)midea_real, 199u, &valid));
    /* Real capture is Fahrenheit: retain native one-Fahrenheit degree steps. */
    assert(AcMidea_Import(&state, &valid) == AC_IMPORT_STATE);
    assert(AcMidea_GetStatus(&state, &status) && status.temp_half == 37u);
    assert(AcMidea_Button(&state, 1u) && (state.raw[2] & 31u) == 4u);
    for (i = 0u; i < 40u; ++i) assert(AcMidea_Button(&state, 1u));
    assert((state.raw[2] & 31u) == 24u);
    for (i = 0u; i < 40u; ++i) assert(AcMidea_Button(&state, 2u));
    assert((state.raw[2] & 31u) == 0u);
    for (i = 0u; i < 6u; ++i) for (bit = 0u; bit < 8u; ++bit) {
        s = valid; s.timings[i] ^= (uint16_t)(1u << bit);
        rejected_signal(&s);
    }
    s = valid; s.length = 5u; rejected_signal(&s);
    s = valid; s.length = 7u; rejected_signal(&s);
    s = valid; s.format = UI_SIGNAL_RAW; rejected_signal(&s);
    s = valid; s.carrier_khz = 0u; rejected_signal(&s);
    s = valid; s.timings[0] = 0x1A1u; rejected_signal(&s);
    s = valid; s.timings[1] = 0x87u; repair(&s); rejected_signal(&s);
    s = valid; s.timings[2] = 0x59u; repair(&s); rejected_signal(&s);
    s = valid; s.timings[0] = 0xA3u; repair(&s); rejected_signal(&s);
    s = valid; s.timings[0] = 0xA2u; s.timings[1] = 1u;
    s.timings[2] = 255u; repair(&s); partial_signal(&s);
    s.timings[1] = 0xEEu; repair(&s); rejected_signal(&s);
    s = valid; s.timings[0] = 0xA4u; repair(&s); partial_signal(&s);
    /* Power off command still contains full MIDEA state. */
    s = valid; s.timings[1] &= 127u; repair(&s);
    assert(AcMidea_Import(&state, &s) == AC_IMPORT_STATE && state.power == 0u);
    assert(AcMidea_Button(&state, 5u) && state.power == 1u);
    /* Pioneer unknown bit, upper temperature bits, timers, sleep are opaque. */
    s = valid; s.timings[1] |= 0x60u; s.timings[2] |= 128u;
    s.timings[3] = 0x65u; s.timings[4] = 0xFFu; repair(&s);
    for (key = 1u; key <= 5u; ++key) {
        assert(AcMidea_Import(&state, &s) == AC_IMPORT_STATE);
        saved = state;
        assert(AcMidea_Button(&state, (uint8_t)key));
        assert((state.raw[1] & 0x60u) == (saved.raw[1] & 0x60u));
        assert((state.raw[2] & 0xE0u) == (saved.raw[2] & 0xE0u));
        assert(state.raw[3] == saved.raw[3] && state.raw[4] == saved.raw[4]);
        assert(AcMidea_Encode(&state, &out));
        repair(&out); assert(out.timings[5] == state.raw[5]);
    }
    for (i = 0u; i < 200u; ++i) wave[i] = AcMidea_Duration(&valid, (uint16_t)i);
    /* Corrupt checksum identically in both normal/inverted phases. */
    wave[83] = wave[83] == 1680u ? 560u : 1680u;
    wave[183] = wave[183] == 1680u ? 560u : 1680u;
    memset(&out, 0xA5, sizeof(out)); s = out;
    assert(!AcMidea_Decode(rd, wave, 200u, &out));
    assert(memcmp(&out, &s, sizeof(out)) == 0);
}

int main(void) {
    UiSignal signal;
    AcState state;
    memset(&signal, 0, sizeof(signal));
    memset(&state, 0xA5, sizeof(state));
    assert(AcMidea_Decode(rd, (void *)midea_real, 199u, &signal));
    assert(AcMidea_Validate(&signal));
    assert(signal.length == 6u);
    assert(signal.timings[0] == 0xA1u && signal.timings[5] == 0x6Eu);
    assert(AcMidea_Import(&state, &signal) == AC_IMPORT_STATE);
    assert(state.power == 1u);
    dynamic_controls();
    streaming_roundtrip();
    validation_and_preservation();
    null_arguments();
    puts("Midea capture, controls, validation and streaming tests passed");
    return 0;
}
