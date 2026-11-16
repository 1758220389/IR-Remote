/* MIDEA48 protocol facts: third_party/MIDEA-COOLIX-NOTICE.txt. */
#include "ac_midea.h"
#include <string.h>

static uint8_t reverse(uint8_t x) {
    uint8_t y;
    unsigned int i;
    y = 0u;
    for (i = 0u; i < 8u; ++i) {
        y = (uint8_t)(((unsigned int)y << 1) | (x & 1u));
        x = (uint8_t)(x >> 1);
    }
    return y;
}
static uint8_t checksum(const uint8_t *b) {
    uint8_t sum;
    unsigned int i;
    sum = 0u;
    for (i = 0u; i < 5u; ++i) sum = (uint8_t)(sum + reverse(b[i]));
    return reverse((uint8_t)(0u - sum));
}
static int kind(const uint8_t *b) {
    static const uint8_t special[] = {1u, 2u, 8u, 9u, 13u, 15u, 18u, 19u};
    unsigned int i;
    if (checksum(b) != b[5]) return 0;
    if (b[0] == 0xA4u) return AC_IMPORT_PARTIAL;
    if (b[0] == 0xA2u) {
        if (b[2] != 255u || b[3] != 255u || b[4] != 255u) return 0;
        for (i = 0u; i < sizeof(special); ++i)
            if (b[1] == special[i]) return AC_IMPORT_PARTIAL;
        return 0;
    }
    if (b[0] != 0xA1u || (b[1] & 7u) > 4u) return 0;
    if ((b[2] & 31u) > ((b[2] & 32u) ? 24u : 13u)) return 0;
    return AC_IMPORT_STATE;
}
static int signal_bytes(const UiSignal *s, uint8_t *b) {
    unsigned int i;
    if (!s || s->format != UI_SIGNAL_MIDEA || s->length != 6u ||
        s->carrier_khz != 38u) return 0;
    for (i = 0u; i < 6u; ++i) {
        if (s->timings[i] > 255u) return 0;
        b[i] = (uint8_t)s->timings[i];
    }
    return kind(b);
}
int AcMidea_Validate(const UiSignal *s) {
    uint8_t b[6];
    return signal_bytes(s, b) != 0;
}
static int near(uint16_t got, uint16_t want) {
    return (unsigned long)got * 100ul >= (unsigned long)want * 70ul &&
           (unsigned long)got * 100ul <= (unsigned long)want * 130ul;
}
int AcMidea_Decode(AcTimingReader reader, void *context, uint16_t count,
                   UiSignal *s) {
    uint8_t b[6], part[6];
    unsigned int frame, frames, i, j, pos;
    uint16_t space;
    if (!reader || !s || count < 199u ||
        (count % 200u != 0u && count % 200u != 199u)) return 0;
    frames = ((unsigned int)count + 1u) / 100u;
    for (frame = 0u; frame < frames; ++frame) {
        pos = frame * 100u;
        if (!near(reader(context, (uint16_t)pos), 4480u) ||
            !near(reader(context, (uint16_t)(pos + 1u)), 4480u)) return 0;
        pos += 2u;
        for (i = 0u; i < 6u; ++i) {
            part[i] = 0u;
            for (j = 0u; j < 8u; ++j) {
                if (!near(reader(context, (uint16_t)pos), 560u)) return 0;
                space = reader(context, (uint16_t)(pos + 1u));
                part[i] = (uint8_t)(part[i] << 1);
                if (near(space, 1680u)) part[i] |= 1u;
                else if (!near(space, 560u)) return 0;
                pos += 2u;
            }
            if (frame == 0u) b[i] = part[i];
            else if (part[i] != (uint8_t)(b[i] ^ ((frame & 1u) ? 255u : 0u)))
                return 0;
        }
        if (!near(reader(context, (uint16_t)pos), 560u)) return 0;
        ++pos;
        if (pos < count && reader(context, (uint16_t)pos) < 3920u) return 0;
    }
    if (!kind(b)) return 0;
    s->format = UI_SIGNAL_MIDEA; s->length = 6u; s->carrier_khz = 38u;
    for (i = 0u; i < 6u; ++i) s->timings[i] = b[i];
    return 1;
}
int AcMidea_Import(AcState *state, const UiSignal *s) {
    uint8_t b[6];
    int result;
    if (!state) return 0;
    result = signal_bytes(s, b);
    if (result != AC_IMPORT_STATE) return result;
    memset(state, 0, sizeof(*state));
    state->format = UI_SIGNAL_MIDEA; state->length = 6u;
    state->power = (uint8_t)(b[1] >> 7);
    memcpy(state->raw, b, 6u);
    return AC_IMPORT_STATE;
}

static int state_valid(const AcState *s) {
    return s && s->format == UI_SIGNAL_MIDEA && s->length == 6u &&
           s->power <= 1u && s->power == (s->raw[1] >> 7) &&
           kind(s->raw) == AC_IMPORT_STATE;
}
int AcMidea_GetStatus(const AcState *s, AcStatus *status) {
    static const uint8_t modes[] = {AC_MODE_COOL, AC_MODE_DRY, AC_MODE_AUTO,
                                    AC_MODE_HEAT, AC_MODE_FAN};
    unsigned int t;
    if (!status || !state_valid(s)) return 0;
    status->power = s->power;
    t = s->raw[2] & 31u;
    /* Fahrenheit native units are retained; report nearest half Celsius. */
    status->temp_half = (uint8_t)((s->raw[2] & 32u) ?
                                ((t + 30u) * 10u + 4u) / 9u : (t + 17u) * 2u);
    status->mode = modes[s->raw[1] & 7u];
    status->fan = (uint8_t)((s->raw[1] >> 3) & 3u);
    return 1;
}
int AcMidea_Button(AcState *s, uint8_t key) {
    static const uint8_t next_mode[] = {1u,3u,0u,4u,2u};
    unsigned int t, max;
    if (!state_valid(s) || key < 1u || key > 5u) return 0;
    if (key <= 2u) {
        t = s->raw[2] & 31u;
        max = (s->raw[2] & 32u) ? 24u : 13u;
        if (key == 1u && t < max) ++t;
        if (key == 2u && t > 0u) --t;
        s->raw[2] = (uint8_t)((s->raw[2] & 0xE0u) | t);
    } else if (key == 3u) {
        s->raw[1] = (uint8_t)((s->raw[1] & 0xF8u) | next_mode[s->raw[1] & 7u]);
    } else if (key == 4u) {
        t = (((s->raw[1] >> 3) & 3u) + 1u) & 3u;
        s->raw[1] = (uint8_t)((s->raw[1] & 0xE7u) | (t << 3));
    } else {
        s->power ^= 1u;
        s->raw[1] ^= 128u;
    }
    s->raw[5] = checksum(s->raw);
    return 1;
}
int AcMidea_Encode(const AcState *s, UiSignal *out) {
    unsigned int i;
    if (!out || !state_valid(s)) return 0;
    out->format = UI_SIGNAL_MIDEA; out->length = 6u; out->carrier_khz = 38u;
    for (i = 0u; i < 6u; ++i) out->timings[i] = s->raw[i];
    return 1;
}

uint16_t AcMidea_DurationCount(const UiSignal *s) {
    return AcMidea_Validate(s) ? 200u : 0u;
}
uint16_t AcMidea_Duration(const UiSignal *s, uint16_t index) {
    uint8_t b[6], value;
    unsigned int i, bit;
    if (index >= 200u || !signal_bytes(s, b)) return 0u;
    i = index % 100u;
    if (i < 2u) return 4480u;
    if (i == 99u) return index >= 100u ? 65535u : 5600u;
    if (!(i & 1u)) return 560u;
    bit = (i - 3u) / 2u;
    value = b[bit / 8u];
    if (index >= 100u) value ^= 255u;
    return (value & (128u >> (bit % 8u))) ? 1680u : 560u;
}
