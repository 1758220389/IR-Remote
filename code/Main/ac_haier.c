/* Copyright 2026. Protocol adapted from IRremoteESP8266, copyright
 * 2018-2021 crankyoldgit. LGPL-2.1-or-later; see HAIER-NOTICE.txt. */
#include <string.h>
#include "ac_haier.h"
static unsigned int length_for(uint16_t format)
{
    if (format == UI_SIGNAL_HAIER) return 9u;
    if (format == UI_SIGNAL_HAIER_YRW02) return 14u;
    return 0u;
}
static int base_command_valid(unsigned int command)
{
    return command == 0u || command == 1u || command == 2u ||
        command == 3u || command == 6u || command == 7u ||
        command == 8u || command == 9u || command == 10u ||
        command == 12u || command == 13u;
}
int AcHaier_Validate(const UiSignal *s)
{
    unsigned int i, n, sum, mode, fan, command;
    if (s == NULL) return 0;
    n = length_for(s->format);
    if (n == 0u || s->length != n || s->carrier_khz != 38u) return 0;
    if (s->timings[0] != (n == 9u ? 0xA5u : 0xA6u)) return 0;
    sum = 0;
    for (i = 0; i < n; i++) {
        if (s->timings[i] > 255u) return 0;
        if (i < n - 1u) sum += s->timings[i];
    }
    if ((sum & 255u) != s->timings[n - 1u] || (s->timings[1] >> 4) > 14u) return 0;
    if (n == 9u) {
        if (!(s->timings[2] & 32u) || (s->timings[6] >> 5) > 4u) return 0;
        command = s->timings[1] & 15u;
        return base_command_valid(command);
    }
    mode = s->timings[7] >> 5;
    fan = s->timings[5] >> 5;
    return !(s->timings[10] & 32u) &&
        (mode == 0u || mode == 1u || mode == 2u || mode == 4u || mode == 6u) &&
        (fan == 1u || fan == 2u || fan == 3u || fan == 5u);
}
uint16_t AcHaier_DurationCount(const UiSignal *s)
{
    if (!AcHaier_Validate(s)) return 0;
    return (uint16_t)(s->length * 16u + 5u);
}
uint16_t AcHaier_Duration(const UiSignal *s, uint16_t index)
{
    unsigned int offset, bit;
    uint16_t count;
    count = AcHaier_DurationCount(s);
    if (index >= count) return 0;
    if (index < 3u) return 3000u;
    if (index == 3u) return 4300u;
    if ((index & 1u) == 0u) return 520u;
    offset = (index - 5u) / 16u;
    bit = 7u - ((index - 5u) % 16u) / 2u;
    return (s->timings[offset] & (1u << bit)) ? 1650u : 650u;
}
static int near_time(uint16_t actual, unsigned int expected)
{
    return actual >= expected * 3u / 4u && actual <= expected * 5u / 4u;
}
int AcHaier_Decode(AcTimingReader reader, void *context, uint16_t count,
                   UiSignal *out)
{
    UiSignal s;
    unsigned int n, i, bit, value;
    uint16_t t;
    if (reader == NULL || out == NULL) return 0;
    if (count == 149u) n = 9u;
    else if (count == 229u) n = 14u;
    else return 0;
    if (!near_time(reader(context, 0), 3000u) ||
        !near_time(reader(context, 1), 3000u) ||
        !near_time(reader(context, 2), 3000u) ||
        !near_time(reader(context, 3), 4300u)) return 0;
    memset(&s, 0, sizeof(s));
    s.length = (uint16_t)n;
    s.format = n == 9u ? UI_SIGNAL_HAIER : UI_SIGNAL_HAIER_YRW02;
    s.carrier_khz = 38u;
    for (i = 0; i < n; i++) {
        value = 0;
        for (bit = 0; bit < 8u; bit++) {
            if (!near_time(reader(context, (uint16_t)(4u + i * 16u + bit * 2u)), 520u)) return 0;
            t = reader(context, (uint16_t)(5u + i * 16u + bit * 2u));
            value <<= 1;
            if (near_time(t, 1650u)) value |= 1u;
            else if (!near_time(t, 650u)) return 0;
        }
        s.timings[i] = (uint16_t)value;
    }
    if (!near_time(reader(context, (uint16_t)(count - 1u)), 520u) ||
        !AcHaier_Validate(&s)) return 0;
    *out = s;
    return 1;
}
/* Work on byte arrays, never implementation-defined C bitfields. */
static void sum_state(AcState *state)
{
    unsigned int i, sum;
    sum = 0;
    for (i = 0; i + 1u < state->length; i++) sum += state->raw[i];
    state->raw[state->length - 1u] = (uint8_t)sum;
}
static int state_valid(const AcState *state)
{
    unsigned int i, sum, mode, fan;
    if (state == NULL || state->power > 1u ||
        length_for(state->format) == 0u ||
        state->length != length_for(state->format)) return 0;
    if (state->raw[0] != (state->length == 9u ? 0xA5u : 0xA6u) ||
        (state->raw[1] >> 4) > 14u) return 0;
    sum = 0;
    for (i = 0; i + 1u < state->length; i++) sum += state->raw[i];
    if ((sum & 255u) != state->raw[state->length - 1u]) return 0;
    if (state->length == 9u)
        return (state->raw[6] >> 5) <= 4u && (state->raw[2] & 32u) &&
            base_command_valid(state->raw[1] & 15u);
    mode = state->raw[7] >> 5;
    fan = state->raw[5] >> 5;
    /* Fahrenheit is a different temperature encoding, not Celsius bits. */
    return !(state->raw[10] & 0x20u) &&
        (mode == 0u || mode == 1u || mode == 2u || mode == 4u || mode == 6u) &&
        (fan == 1u || fan == 2u || fan == 3u || fan == 5u);
}
int AcHaier_GetStatus(const AcState *state, AcStatus *status)
{
    AcStatus result;
    unsigned int fan, mode;
    if (status == NULL || !state_valid(state)) return 0;
    result.power = state->power;
    result.temp_half = (uint8_t)(32u + (state->raw[1] >> 4) * 2u);
    if (state->length == 9u) {
        mode = state->raw[6] >> 5;
        fan = state->raw[5] >> 6;
        result.fan = (uint8_t)(fan == 0u ? 0u : 4u - fan);
    } else {
        mode = state->raw[7] >> 5;
        if (mode == 4u) mode = AC_MODE_HEAT;
        else if (mode == 6u) mode = AC_MODE_FAN;
        fan = state->raw[5] >> 5;
        result.fan = (uint8_t)(fan == 5u ? 0u : 4u - fan);
    }
    result.mode = (uint8_t)mode;
    *status = result;
    return 1;
}
int AcHaier_Import(AcState *state, const UiSignal *signal)
{
    AcState next;
    unsigned int i, command, prior;
    if (state == NULL || !AcHaier_Validate(signal)) return 0;
    memset(&next, 0, sizeof(next));
    next.format = signal->format;
    next.length = (uint8_t)signal->length;
    for (i = 0; i < signal->length; i++) next.raw[i] = (uint8_t)signal->timings[i];
    prior = state_valid(state) && state->format == next.format;
    if (next.length == 9u) {
        command = next.raw[1] & 15u;
        if (!state_valid(&next)) return 0;
        if (command != 1u && !prior) return AC_IMPORT_PARTIAL;
        if (command == 0u) {
            next = *state;
            next.power = 0;
        } else next.power = command == 1u ? 1u : state->power;
    } else {
        next.power = (uint8_t)((next.raw[4] >> 6) & 1u);
        if (!state_valid(&next)) return 0;
        if (!next.power && prior) {
            next = *state;
            next.power = 0;
        }
    }
    *state = next;
    return AC_IMPORT_STATE;
}
int AcHaier_Encode(const AcState *state, UiSignal *signal)
{
    UiSignal out;
    unsigned int i, sum;
    if (signal == NULL || !state_valid(state)) return 0;
    memset(&out, 0, sizeof(out));
    out.format = state->format;
    out.length = state->length;
    out.carrier_khz = 38u;
    for (i = 0; i < state->length; i++) out.timings[i] = state->raw[i];
    if (state->length == 9u) {
        if (!state->power) out.timings[1] &= 240u;
        else if (!(out.timings[1] & 15u)) out.timings[1] |= 1u;
    } else {
        if (((out.timings[4] >> 6) & 1u) != state->power)
            out.timings[12] = (uint16_t)((out.timings[12] & 224u) | 5u);
        out.timings[4] = (uint16_t)((out.timings[4] & 191u) | (state->power ? 64u : 0u));
    }
    sum = 0;
    for (i = 0; i + 1u < state->length; i++) sum += out.timings[i];
    out.timings[state->length - 1u] = (uint16_t)(sum & 255u);
    *signal = out;
    return 1;
}
int AcHaier_Button(AcState *state, uint8_t key)
{
    AcStatus status;
    unsigned int base, command, value, mode;
    static const uint8_t modes[5] = {0, 1, 2, 4, 6};
    if (key < 1u || key > 5u || !AcHaier_GetStatus(state, &status)) return 0;
    base = state->length == 9u;
    command = 0;
    if (key == 1u || key == 2u) {
        value = state->raw[1] >> 4;
        if (key == 1u && value < 14u) value++;
        if (key == 2u && value > 0u) value--;
        /* Like upstream, saturation does not invent a changed-temp command. */
        if (value == (unsigned int)(state->raw[1] >> 4)) return 1;
        state->raw[1] = (uint8_t)((state->raw[1] & 15u) | (value << 4));
        command = key == 1u ? (base ? 6u : 0u) : (base ? 7u : 1u);
    } else if (key == 3u) {
        mode = (status.mode + 1u) % 5u;
        if (base) state->raw[6] = (uint8_t)((state->raw[6] & 31u) | (mode << 5));
        else {
            state->raw[7] = (uint8_t)((state->raw[7] & 31u) | ((unsigned int)modes[mode] << 5));
            if (mode != AC_MODE_COOL && mode != AC_MODE_HEAT) state->raw[6] &= 63u;
        }
        command = base ? 2u : 6u;
    } else if (key == 4u) {
        value = (status.fan + 1u) % 4u;
        value = value == 0u ? (base ? 0u : 5u) : 4u - value;
        state->raw[5] = (uint8_t)((state->raw[5] & (base ? 63u : 31u)) | (value << (base ? 6u : 5u)));
        command = base ? 3u : 4u;
    } else {
        state->power ^= 1u;
        command = base ? state->power : 5u;
    }
    if (base) state->raw[1] = (uint8_t)((state->raw[1] & 240u) | command);
    else state->raw[12] = (uint8_t)((state->raw[12] & 224u) | command);
    sum_state(state);
    return 1;
}
