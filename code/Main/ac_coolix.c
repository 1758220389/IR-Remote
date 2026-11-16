/* COOLIX24 protocol facts: third_party/MIDEA-COOLIX-NOTICE.txt. */
#include "ac_coolix.h"
#include <string.h>

static const uint8_t temp_map[14] = {0u,1u,3u,2u,6u,7u,5u,4u,12u,13u,9u,8u,10u,11u};
static int temp_index(uint8_t code) {
    unsigned int i;
    for (i = 0u; i < 14u; ++i) if (temp_map[i] == code) return (int)i;
    return -1;
}
static int kind(const uint8_t *b) {
    static const unsigned long special[] = {
        0xB27BE0ul, 0xB26BE0ul, 0xB5F5A2ul, 0xB20FE0ul,
        0xB2E003ul, 0xB5F5A5ul, 0xB5F5AAul
    };
    unsigned long value;
    unsigned int i, fan, mode, temp;
    value = ((unsigned long)b[0] << 16) | ((unsigned long)b[1] << 8) | b[2];
    for (i = 0u; i < sizeof(special)/sizeof(special[0]); ++i)
        if (value == special[i]) return AC_IMPORT_PARTIAL;
    if ((b[0] & 0xF0u) != 0xB0u) return 0;
    fan = b[1] >> 5; mode = (b[2] >> 2) & 3u; temp = b[2] >> 4;
    /* Extended Fahrenheit/fixed and zone-follow fan variants not dynamic here. */
    if (fan == 3u || fan == 6u || fan == 7u) return 0;
    if (temp_index((uint8_t)temp) < 0 && !(mode == 1u && temp == 14u)) return 0;
    if (fan == 0u && (mode == 0u || mode == 3u || temp == 14u)) return 0;
    if (fan == 5u && (mode == 2u || (mode == 1u && temp != 14u))) return 0;
    return AC_IMPORT_STATE;
}
static int signal_bytes(const UiSignal *s, uint8_t *b) {
    unsigned int i;
    if (!s || s->format != UI_SIGNAL_COOLIX || s->length != 3u ||
        s->carrier_khz != 38u) return 0;
    for (i = 0u; i < 3u; ++i) {
        if (s->timings[i] > 255u) return 0;
        b[i] = (uint8_t)s->timings[i];
    }
    return kind(b);
}
int AcCoolix_Validate(const UiSignal *s) {
    uint8_t b[3];
    return signal_bytes(s, b) != 0;
}
static int near(uint16_t got, uint16_t want) {
    return (unsigned long)got * 100ul >= (unsigned long)want * 70ul &&
           (unsigned long)got * 100ul <= (unsigned long)want * 130ul;
}
int AcCoolix_Decode(AcTimingReader reader, void *context, uint16_t count,
                    UiSignal *s) {
    uint8_t b[3], part[6];
    unsigned int frame, frames, i, j, pos;
    uint16_t space;
    if (!reader || !s || count < 99u ||
        (count % 100u != 0u && count % 100u != 99u)) return 0;
    frames = ((unsigned int)count + 1u) / 100u;
    for (frame = 0u; frame < frames; ++frame) {
        pos = frame * 100u;
        if (!near(reader(context, (uint16_t)pos), 4692u) ||
            !near(reader(context, (uint16_t)(pos + 1u)), 4416u)) return 0;
        pos += 2u;
        for (i = 0u; i < 6u; ++i) {
            part[i] = 0u;
            for (j = 0u; j < 8u; ++j) {
                if (!near(reader(context, (uint16_t)pos), 552u)) return 0;
                space = reader(context, (uint16_t)(pos + 1u));
                part[i] = (uint8_t)(part[i] << 1);
                if (near(space, 1656u)) part[i] |= 1u;
                else if (!near(space, 552u)) return 0;
                pos += 2u;
            }
            if (i & 1u) {
                if ((unsigned int)part[i] + part[i - 1u] != 255u) return 0;
            } else if (frame == 0u) b[i / 2u] = part[i];
            else if (part[i] != b[i / 2u]) return 0;
        }
        if (!near(reader(context, (uint16_t)pos), 552u)) return 0;
        ++pos;
        if (pos < count && reader(context, (uint16_t)pos) < 3670u) return 0;
    }
    if (!kind(b)) return 0;
    s->format = UI_SIGNAL_COOLIX; s->length = 3u; s->carrier_khz = 38u;
    for (i = 0u; i < 3u; ++i) s->timings[i] = b[i];
    return 1;
}
int AcCoolix_Import(AcState *state, const UiSignal *s) {
    uint8_t b[3];
    int result;
    if (!state) return 0;
    result = signal_bytes(s, b);
    if (result != AC_IMPORT_STATE) return result;
    memset(state, 0, sizeof(*state));
    state->format = UI_SIGNAL_COOLIX; state->length = 3u; state->power = 1u;
    memcpy(state->raw, b, 3u);
    return AC_IMPORT_STATE;
}

static int state_valid(const AcState *s) {
    return s && s->format == UI_SIGNAL_COOLIX && s->length == 3u &&
           s->power <= 1u && kind(s->raw) == AC_IMPORT_STATE;
}
int AcCoolix_GetStatus(const AcState *s, AcStatus *status) {
    static const uint8_t modes[] = {AC_MODE_COOL, AC_MODE_DRY, AC_MODE_AUTO, AC_MODE_HEAT};
    static const uint8_t fans[] = {AC_FAN_AUTO, AC_FAN_HIGH, AC_FAN_MEDIUM,
                                   0u, AC_FAN_LOW, AC_FAN_AUTO};
    unsigned int mode;
    int t;
    if (!status || !state_valid(s)) return 0;
    mode = (s->raw[2] >> 2) & 3u;
    t = temp_index((uint8_t)(s->raw[2] >> 4));
    status->power = s->power;
    status->mode = (uint8_t)((mode == 1u && t < 0) ? AC_MODE_FAN : modes[mode]);
    /* Fan-only has no set-point in the protocol; zero means unavailable. */
    status->temp_half = (uint8_t)(t < 0 ? 0 : (t + 17) * 2);
    status->fan = fans[s->raw[1] >> 5];
    return 1;
}
int AcCoolix_Button(AcState *s, uint8_t key) {
    static const uint8_t next_modes[] = {1u,3u,0u,1u};
    static const uint8_t next_fans[] = {4u,0u,1u,0u,2u,4u};
    AcStatus status;
    unsigned int mode, fan;
    int t;
    if (!AcCoolix_GetStatus(s, &status) || key < 1u || key > 5u) return 0;
    if (key <= 2u) {
        t = temp_index((uint8_t)(s->raw[2] >> 4));
        if (t < 0) return 0; /* No temperature in fan-only mode. */
        if (key == 1u && t < 13) ++t;
        if (key == 2u && t > 0) --t;
        s->raw[2] = (uint8_t)((s->raw[2] & 15u) | ((unsigned int)temp_map[t] << 4));
    } else if (key == 3u) {
        mode = (s->raw[2] >> 2) & 3u;
        if (status.mode == AC_MODE_FAN) {
            /* Upstream's native Fan -> Auto behavior resolves the absent
             * set-point to its maximum 30C. No invented state on import. */
            s->raw[2] = (uint8_t)((s->raw[2] & 3u) | ((unsigned int)temp_map[13] << 4) | 8u);
            mode = 2u;
        } else if (status.mode == AC_MODE_HEAT) {
            s->raw[2] = (uint8_t)((s->raw[2] & 3u) | 0xE4u);
            mode = 1u;
        } else {
            mode = next_modes[mode];
            s->raw[2] = (uint8_t)((s->raw[2] & 0xF3u) | (mode << 2));
        }
        fan = (mode == 2u || (mode == 1u && status.mode != AC_MODE_HEAT)) ? 0u : 5u;
        s->raw[1] = (uint8_t)((s->raw[1] & 31u) | (fan << 5));
    } else if (key == 4u) {
        fan = next_fans[s->raw[1] >> 5];
        if (fan == 0u && status.mode != AC_MODE_AUTO && status.mode != AC_MODE_DRY)
            fan = 5u;
        s->raw[1] = (uint8_t)((s->raw[1] & 31u) | (fan << 5));
    } else {
        s->power ^= 1u; /* Never replace retained ON bytes with OFF command. */
    }
    return 1;
}
int AcCoolix_Encode(const AcState *s, UiSignal *out) {
    static const uint8_t off[] = {0xB2u,0x7Bu,0xE0u};
    unsigned int i;
    if (!out || !state_valid(s)) return 0;
    out->format = UI_SIGNAL_COOLIX; out->length = 3u; out->carrier_khz = 38u;
    for (i = 0u; i < 3u; ++i) out->timings[i] = s->power ? s->raw[i] : off[i];
    return 1;
}

uint16_t AcCoolix_DurationCount(const UiSignal *s) {
    return AcCoolix_Validate(s) ? 200u : 0u;
}
uint16_t AcCoolix_Duration(const UiSignal *s, uint16_t index) {
    uint8_t b[3], value;
    unsigned int i, bit, byte;
    if (index >= 200u || !signal_bytes(s, b)) return 0u;
    i = index % 100u;
    if (i == 0u) return 4692u;
    if (i == 1u) return 4416u;
    if (i == 99u) return index >= 100u ? 65535u : 5244u;
    if (!(i & 1u)) return 552u;
    bit = (i - 3u) / 2u; byte = bit / 8u;
    value = b[byte / 2u];
    if (byte & 1u) value ^= 255u;
    return (value & (128u >> (bit % 8u))) ? 1656u : 552u;
}
