#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ac_coolix.h"
/* Published capture: see tests/fixtures/upstream_coolix_off579.json. */
static const uint16_t coolix_off579[] = {
    4444, 4434, 590, 1578, 698, 446, 590, 1578, 622, 1596, 622, 500,
    644, 476, 644, 1548, 588, 532, 594, 530, 612, 1578, 590, 532,
    588, 534, 672, 1518, 594, 1598, 590, 510, 612, 1580, 644, 480,
    612, 1578, 644, 1548, 644, 1548, 594, 1598, 642, 506, 644, 1550,
    644, 1548, 594, 1600, 644, 478, 644, 478, 642, 480, 644, 478,
    642, 1548, 594, 530, 590, 532, 614, 1578, 644, 1548, 594, 1600,
    588, 534, 566, 556, 588, 530, 590, 532, 586, 514, 612, 532,
    588, 532, 590, 534, 588, 1578, 642, 1576, 642, 1550, 588, 1602,
    588, 1580, 642, 4712, 4546, 4406, 588, 1606, 642, 478, 644, 1550,
    590, 1604, 588, 534, 586, 532, 586, 1582, 642, 480, 642, 480,
    668, 1550, 642, 480, 642, 478, 642, 1552, 612, 1578, 586, 538,
    588, 1580, 674, 472, 590, 1602, 586, 1580, 618, 1576, 642, 1548,
    594, 530, 590, 1584, 608, 1578, 644, 1550, 642, 480, 642, 478,
    642, 480, 642, 480, 642, 1550, 590, 530, 592, 528, 592, 1602,
    642, 1548, 592, 1604, 586, 584, 642, 480, 640, 480, 640, 480,
    642, 480, 642, 480, 642, 480, 642, 480, 642, 1552, 590, 1604,
    588, 1578, 642, 1552, 640, 1550, 592
};
/* Published capture: see tests/fixtures/upstream_coolix_off1748.json. */
static const uint16_t coolix_off1748[] = {
    4642, 4502, 514, 1706, 516, 624, 488, 1704, 514, 1702, 516, 624,
    488, 624, 488, 1702, 514, 626, 488, 620, 488, 1702, 490, 620,
    512, 620, 488, 1728, 488, 1704, 514, 624, 488, 1704, 514, 620,
    488, 1702, 490, 1722, 488, 1724, 514, 1728, 488, 600, 512, 1728,
    490, 1706, 512, 1698, 488, 646, 486, 622, 462, 646, 488, 624,
    488, 1704, 514, 626, 488, 628, 460, 1724, 514, 1702, 514, 1724,
    462, 646, 488, 624, 488, 624, 488, 626, 486, 602, 488, 646,
    460, 648, 486, 626, 488, 1704, 486, 1724, 488, 1748, 488, 1704,
    514, 1708, 488, 5312, 4648, 4494, 488, 1704, 486, 646, 486, 1698,
    512, 1700, 488, 646, 462, 646, 486, 1728, 462, 648, 484, 622,
    462, 1724, 510, 622, 488, 626, 488, 1702, 514, 1728, 490, 626,
    488, 1730, 462, 646, 488, 1704, 512, 1724, 486, 1698, 514, 1728,
    488, 626, 488, 1728, 488, 1704, 514, 1700, 512, 620, 486, 620,
    488, 620, 486, 626, 490, 1728, 488, 626, 488, 628, 460, 1750,
    488, 1728, 488, 1704, 488, 646, 488, 620, 488, 624, 488, 626,
    488, 626, 462, 646, 462, 644, 488, 626, 488, 1728, 490, 1704,
    486, 1724, 514, 1724, 488, 1728, 488
};
/* Published capture: see tests/fixtures/upstream_coolix_swing1318.json. */
static const uint16_t coolix_swing1318[] = {
    4386, 4366, 546, 1622, 520, 522, 548, 1622, 496, 1646, 520, 524,
    548, 522, 548, 1622, 522, 522, 548, 520, 550, 1620, 520, 522,
    548, 522, 546, 1622, 496, 1646, 522, 520, 526, 1646, 520, 522,
    548, 522, 524, 546, 550, 520, 550, 1620, 498, 1644, 520, 1622,
    522, 1622, 520, 1620, 522, 1620, 524, 1618, 524, 1618, 524, 520,
    550, 520, 550, 522, 550, 518, 552, 1618, 524, 1618, 524, 1618,
    524, 520, 550, 520, 550, 520, 550, 520, 550, 520, 552, 516,
    552, 518, 550, 522, 550, 1618, 526, 1616, 524, 1618, 524, 1618,
    524, 1618, 550
};
static const uint16_t coolix48_timer[] = {
    4342, 4454, 486, 1724, 436, 658, 438, 1748, 464, 1718, 462, 634,
    440, 656, 462, 1696, 488, 634, 462, 634, 436, 1722, 516, 608,
    462, 660, 436, 1694, 488, 1720, 440, 630, 488, 1700, 488, 1704,
    458, 660, 462, 1698, 490, 632, 462, 634, 436, 684, 436, 1700,
    464, 1748, 462, 634, 462, 1720, 436, 658, 462, 1700, 488, 1692,
    512, 1696, 438, 684, 410, 686, 434, 688, 408, 1696, 488, 1694,
    464, 682, 414, 1748, 436, 1722, 488, 632, 438, 686, 408, 662,
    462, 1696, 488, 1722, 462, 1696, 462, 1746, 436, 1798, 386, 1694,
    490, 1720, 516, 5234, 4370, 4446, 490, 1690, 492, 658, 434, 1726,
    436, 1746, 464, 604, 488, 658, 412, 1718, 490, 636, 460, 660,
    438, 1698, 460, 662, 458, 632, 436, 1718, 490, 1720, 488, 608,
    436, 1754, 462, 1726, 438, 682, 414, 1748, 464, 632, 460, 660,
    410, 658, 438, 1748, 464, 1694, 464, 660, 436, 1720, 488, 634,
    460, 1726, 462, 1724, 462, 1692, 490, 606, 462, 714, 384, 660,
    460, 1722, 460, 1722, 490, 606, 464, 1718, 490, 1670, 486, 634,
    462, 662, 410, 660, 460, 1722, 464, 1718, 460, 1696, 464, 1720,
    462, 1720, 462, 1722, 486, 1700, 462
};
static uint16_t rd(void *p, uint16_t i) { return ((const uint16_t *)p)[i]; }
static const uint8_t MODES[5] = {AC_MODE_COOL,AC_MODE_DRY,AC_MODE_HEAT,AC_MODE_FAN,AC_MODE_AUTO};
static const uint8_t FANS[4] = {AC_FAN_LOW,AC_FAN_MEDIUM,AC_FAN_HIGH,AC_FAN_AUTO};
static void dynamic_controls(void) {
    static const uint8_t native[] = {0xB2u,0x1Fu,0xC8u};
    UiSignal s, out;
    AcState state, saved;
    AcStatus status;
    unsigned int i;
    memset(&s, 0, sizeof(s));
    s.format = UI_SIGNAL_COOLIX; s.length = 3u; s.carrier_khz = 38u;
    for (i = 0u; i < 3u; ++i) s.timings[i] = native[i];
    assert(AcCoolix_Import(&state, &s) == AC_IMPORT_STATE);
    assert(AcCoolix_GetStatus(&state, &status));
    assert(status.power == 1u && status.temp_half == 50u);
    assert(AcCoolix_Encode(&state, &out));
    for (i = 0u; i < 3u; ++i) assert(out.timings[i] == native[i]);
    assert(AcCoolix_Button(&state, 1u));
    assert(AcCoolix_GetStatus(&state, &status) && status.temp_half == 52u);
    assert(AcCoolix_Button(&state, 2u));
    assert(AcCoolix_GetStatus(&state, &status) && status.temp_half == 50u);
    for (i = 0u; i < 40u; ++i) assert(AcCoolix_Button(&state, 1u));
    assert(AcCoolix_GetStatus(&state, &status) && status.temp_half == 60u);
    for (i = 0u; i < 40u; ++i) assert(AcCoolix_Button(&state, 2u));
    assert(AcCoolix_GetStatus(&state, &status) && status.temp_half == 34u);
    for (i = 0u; i < 5u; ++i) {
        assert(AcCoolix_Button(&state, 3u));
        assert(AcCoolix_GetStatus(&state, &status));
        assert(status.mode == MODES[i]);
        assert(AcCoolix_Encode(&state, &out) && AcCoolix_Validate(&out));
    }
    /* Cycle from auto through all four canonical fan choices. */
    for (i = 0u; i < 4u; ++i) {
        assert(AcCoolix_Button(&state, 4u));
        assert(AcCoolix_GetStatus(&state, &status));
        assert(status.fan == FANS[i]);
    }
    saved = state;
    assert(!AcCoolix_Button(&state, 0u));
    assert(!AcCoolix_Button(&state, 6u));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(AcCoolix_Button(&state, 5u));
    assert(AcCoolix_GetStatus(&state, &status) && !status.power);
    assert(AcCoolix_Encode(&state, &out) && AcCoolix_Validate(&out));
    assert(out.timings[0] == 0xB2u && out.timings[1] == 0x7Bu && out.timings[2] == 0xE0u);
    assert(memcmp(state.raw, saved.raw, sizeof(state.raw)) == 0);
    assert(AcCoolix_Button(&state, 5u));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    state.length = 0u; saved = state;
    assert(!AcCoolix_GetStatus(&state, &status));
    assert(!AcCoolix_Button(&state, 1u));
    assert(!AcCoolix_Encode(&state, &out));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
}

static uint16_t stream_read(void *p, uint16_t i) {
    return AcCoolix_Duration((const UiSignal *)p, i);
}
static void streaming_roundtrip(void) {
    static const uint8_t payload[] = {0xB2u,0x1Fu,0xC8u};
    UiSignal s, decoded, unchanged;
    uint16_t wave[401];
    unsigned int i, j, byte, bit, frame;
    uint8_t value;
    memset(&s, 0, sizeof(s));
    s.format = UI_SIGNAL_COOLIX; s.length = 3u; s.carrier_khz = 38u;
    for (i = 0u; i < 3u; ++i) s.timings[i] = payload[i];
    assert(AcCoolix_DurationCount(&s) == 200u);
    for (i = 0u; i < 200u; ++i) {
        wave[i] = AcCoolix_Duration(&s, (uint16_t)i);
        assert(wave[i] != 0u);
    }
    assert(AcCoolix_Duration(&s, 200u) == 0u);
    assert(AcCoolix_Duration(&s, 65535u) == 0u);
    for (frame = 0u; frame < 2u; ++frame) {
        j = frame * 100u;
        assert(wave[j++] == 4692u && wave[j++] == 4416u);
        for (byte = 0u; byte < 6u; ++byte) {
            value = payload[byte / 2u]; if (byte & 1u) value ^= 255u;
            for (bit = 0u; bit < 8u; ++bit) {
                assert(wave[j++] == 552u);
                assert(wave[j++] == ((value & (128u >> bit)) ? 1656u : 552u));
            }
        }
        assert(wave[j++] == 552u);
        assert(wave[j] == (frame ? 65535u : 5244u));
    }
    assert(AcCoolix_Decode(stream_read, &s, 200u, &decoded));
    for (i = 0u; i < 3u; ++i) assert(s.timings[i] == decoded.timings[i]);
    assert(AcCoolix_Decode(rd, wave, 199u, &decoded));
    /* Header, data marks/spaces, footer, and separator corruption. */
    memset(&unchanged, 0xA5, sizeof(unchanged));
    for (i = 0u; i < 199u; ++i) {
        uint16_t old;
        old = wave[i]; wave[i] = 1u; decoded = unchanged;
        assert(!AcCoolix_Decode(rd, wave, 199u, &decoded));
        assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
        wave[i] = old;
    }
    /* Valid-looking data corruption must fail inverted-copy integrity. */
    wave[3] = wave[3] == 1656u ? 552u : 1656u;
    decoded = unchanged;
    assert(!AcCoolix_Decode(rd, wave, 200u, &decoded));
    assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
    wave[3] = wave[3] == 1656u ? 552u : 1656u;
    /* Every non-envelope truncation is rejected; one Coolix frame is legal. */
    for (i = 0u; i < 199u; ++i) {
        if (i == 99u || i == 100u) continue;
        decoded = unchanged;
        assert(!AcCoolix_Decode(rd, wave, (uint16_t)i, &decoded));
        assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
    }
    for (i = 0u; i < 200u; ++i) wave[i + 200u] = wave[i];
    assert(AcCoolix_Decode(rd, wave, 400u, &decoded));
    assert(AcCoolix_Decode(rd, wave, 399u, &decoded));
    wave[400] = 560u;
    assert(!AcCoolix_Decode(rd, wave, 401u, &decoded));
    assert(!AcCoolix_Decode(0, 0, 200u, &decoded));
    assert(!AcCoolix_Decode(rd, wave, 200u, 0));
    s.timings[0] = 0u;
    assert(!AcCoolix_DurationCount(&s));
    assert(!AcCoolix_Duration(&s, 0u));
    assert(!AcCoolix_DurationCount(0));
    assert(!AcCoolix_Duration(0, 0u));
}

static void rejected_signal(const UiSignal *s) {
    AcState state, saved;
    UiSignal output, original;
    AcStatus status, prior;
    memset(&state, 0xA5, sizeof(state)); saved = state;
    assert(!AcCoolix_Validate(s));
    assert(!AcCoolix_Import(&state, s));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(!AcCoolix_DurationCount(s));
    assert(!AcCoolix_Duration(s, 0u));
    memset(&output, 0xA5, sizeof(output)); original = output;
    assert(!AcCoolix_Encode(&state, &output));
    assert(memcmp(&output, &original, sizeof(output)) == 0);
    memset(&status, 0xA5, sizeof(status)); prior = status;
    assert(!AcCoolix_GetStatus(&state, &status));
    assert(memcmp(&status, &prior, sizeof(status)) == 0);
}
static void partial_signal(UiSignal *s) {
    AcState state, saved;
    UiSignal decoded;
    memset(&state, 0xA5, sizeof(state)); saved = state;
    assert(AcCoolix_Validate(s));
    assert(AcCoolix_Import(&state, s) == AC_IMPORT_PARTIAL);
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(AcCoolix_Decode(stream_read, s, AcCoolix_DurationCount(s), &decoded));
}
static void null_arguments(void) {
    AcState state;
    UiSignal s;
    AcStatus status;
    memset(&state, 0, sizeof(state));
    memset(&s, 0, sizeof(s));
    assert(!AcCoolix_Validate(0));
    assert(!AcCoolix_Import(0, &s));
    assert(!AcCoolix_Import(&state, 0));
    assert(!AcCoolix_GetStatus(0, &status));
    assert(!AcCoolix_GetStatus(&state, 0));
    assert(!AcCoolix_Button(0, 1u));
    assert(!AcCoolix_Encode(0, &s));
    assert(!AcCoolix_Encode(&state, 0));
}

static void validation_and_preservation(void) {
    static const unsigned long partial[] = {0xB27BE0ul,0xB26BE0ul,0xB5F5A2ul,
        0xB20FE0ul,0xB2E003ul,0xB5F5A5ul,0xB5F5AAul};
    UiSignal s, valid, out;
    AcState state, saved;
    AcStatus status;
    unsigned int i, key;
    memset(&valid, 0, sizeof(valid));
    valid.format = UI_SIGNAL_COOLIX; valid.length = 3u; valid.carrier_khz = 38u;
    valid.timings[0] = 0xB2u; valid.timings[1] = 0x1Fu; valid.timings[2] = 0xC8u;
    s = valid; s.length = 2u; rejected_signal(&s);
    s = valid; s.length = 4u; rejected_signal(&s);
    s = valid; s.format = UI_SIGNAL_RAW; rejected_signal(&s);
    s = valid; s.carrier_khz = 0u; rejected_signal(&s);
    s = valid; s.timings[0] = 0x1B2u; rejected_signal(&s);
    s = valid; s.timings[0] = 0xA2u; rejected_signal(&s);
    s = valid; s.timings[1] = 0x7Fu; rejected_signal(&s);
    s = valid; s.timings[1] = 0xDFu; rejected_signal(&s);
    s = valid; s.timings[1] = 0xFFu; rejected_signal(&s);
    s = valid; s.timings[2] = 0xF8u; rejected_signal(&s);
    s = valid; s.timings[2] = 0xE8u; rejected_signal(&s);
    /* Auto0 is native to Auto/Dry; Auto5 is native to Cool/Heat/Fan. */
    s = valid; s.timings[2] = 0xC0u; rejected_signal(&s);
    s = valid; s.timings[1] = 0xBFu; rejected_signal(&s);
    for (i = 0u; i < sizeof(partial)/sizeof(partial[0]); ++i) {
        s = valid; s.timings[0] = (uint16_t)(partial[i] >> 16);
        s.timings[1] = (uint16_t)((partial[i] >> 8) & 255ul);
        s.timings[2] = (uint16_t)(partial[i] & 255ul); partial_signal(&s);
    }
    s = valid; s.timings[0] |= 5u; s.timings[2] |= 1u;
    for (key = 1u; key <= 5u; ++key) {
        assert(AcCoolix_Import(&state, &s) == AC_IMPORT_STATE); saved = state;
        assert(AcCoolix_Button(&state, (uint8_t)key));
        assert(state.raw[0] == saved.raw[0]);
        assert((state.raw[1] & 31u) == (saved.raw[1] & 31u));
        assert((state.raw[2] & 3u) == (saved.raw[2] & 3u));
        assert(AcCoolix_Encode(&state, &out) && AcCoolix_Validate(&out));
    }
    /* Native fan-only state has no temperature, and cannot invent one. */
    s = valid; s.timings[1] = 0xBFu; s.timings[2] = 0xE4u;
    assert(AcCoolix_Import(&state, &s) == AC_IMPORT_STATE); saved = state;
    assert(AcCoolix_GetStatus(&state, &status));
    assert(status.mode == AC_MODE_FAN && status.temp_half == 0u);
    assert(!AcCoolix_Button(&state, 1u));
    assert(!AcCoolix_Button(&state, 2u));
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(AcCoolix_Button(&state, 3u));
    assert(AcCoolix_GetStatus(&state, &status));
    assert(status.mode == AC_MODE_AUTO && status.temp_half == 60u);
    memset(&out, 0xA5, sizeof(out)); s = out;
    assert(!AcCoolix_Decode(rd, (void *)coolix48_timer, 199u, &out));
    assert(memcmp(&out, &s, sizeof(out)) == 0);
}

int main(void) {
    UiSignal signal;
    AcState state;
    memset(&signal, 0, sizeof(signal));
    memset(&state, 0xA5, sizeof(state));
    assert(AcCoolix_Decode(rd, (void *)coolix_off579, 199u, &signal));
    assert(AcCoolix_Validate(&signal));
    assert(signal.length == 3u);
    assert(signal.timings[0] == 0xB2u && signal.timings[2] == 0xE0u);
    assert(AcCoolix_Import(&state, &signal) == AC_IMPORT_PARTIAL);
    assert(state.format == 0xA5A5u);
    assert(AcCoolix_Decode(rd, (void *)coolix_off1748, 199u, &signal));
    assert(AcCoolix_Decode(rd, (void *)coolix_swing1318, 99u, &signal));
    assert(AcCoolix_Import(&state, &signal) == AC_IMPORT_PARTIAL);
    dynamic_controls();
    streaming_roundtrip();
    validation_and_preservation();
    null_arguments();
    puts("Coolix capture, controls, validation and streaming tests passed");
    return 0;
}
