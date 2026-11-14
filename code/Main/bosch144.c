/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Portable adaptation of IRremoteESP8266 ir_Bosch.h/.cpp.
 * Copyright 2022 Nico Thien; Copyright 2022 David Conran.
 * See third_party/IRremoteESP8266-LICENSE.txt and BOSCH144-NOTICE.txt.
 */
#include "bosch144.h"
#include <string.h>
static const uint8_t temp_map[15] = {
    2u, 0u, 4u, 12u, 8u, 24u, 28u, 20u, 16u, 48u, 52u, 36u, 32u, 40u, 44u
};
static const uint8_t off_bytes[12] = {
    0xB2u,0x4Du,0x7Bu,0x84u,0xE0u,0x1Fu,
    0xB2u,0x4Du,0x7Bu,0x84u,0xE0u,0x1Fu
};
static int valid_bytes(const uint8_t *raw, uint16_t length) {
    uint16_t i;
    uint8_t sum;
    if (length != 12u && length != 18u) return 0;
    if (raw[0] != 0xB2u || memcmp(raw, raw + 6, 6u) != 0) return 0;
    for (i = 0u; i < 12u; i += 2u)
        if ((unsigned int)raw[i] + raw[i + 1u] != 255u) return 0;
    if (length == 12u) return memcmp(raw, off_bytes, 12u) == 0;
    if (raw[12] != 0xD5u) return 0;
    sum = 0u;
    for (i = 12u; i < 17u; ++i) sum = (uint8_t)(sum + raw[i]);
    return sum == raw[17];
}
int Bosch144_ValidateSignal(const UiSignal *signal) {
    uint8_t raw[18];
    uint16_t i;
    if (signal == 0 || signal->format != UI_SIGNAL_BOSCH144 ||
        signal->carrier_khz != 38u ||
        (signal->length != 12u && signal->length != 18u)) return 0;
    for (i = 0u; i < signal->length; ++i) {
        if (signal->timings[i] > 255u) return 0;
        raw[i] = (uint8_t)signal->timings[i];
    }
    return valid_bytes(raw, signal->length);
}
static int near_timing(uint16_t value, uint16_t nominal) {
    return (uint32_t)value * 4u >= (uint32_t)nominal * 3u &&
           (uint32_t)value * 4u <= (uint32_t)nominal * 5u;
}
static int bit_mark(uint16_t value) { return value >= 250u && value <= 850u; }
int Bosch144_Decode(Bosch144TimingReader reader, void *context,
                    uint16_t count, UiSignal *out) {
    uint8_t raw[18];
    uint16_t sections;
    uint16_t section;
    uint16_t bit;
    uint16_t pos;
    uint16_t space;
    uint16_t byte_index;
    uint16_t i;
    if (reader == 0 || out == 0 || (count != 299u && count != 199u)) return 0;
    sections = (uint16_t)((count + 1u) / 100u);
    memset(raw, 0, sizeof(raw));
    for (section = 0u; section < sections; ++section) {
        pos = (uint16_t)(section * 100u);
        if (!near_timing(reader(context, pos), 4366u) ||
            !near_timing(reader(context, (uint16_t)(pos + 1u)), 4415u)) return 0;
        for (bit = 0u; bit < 48u; ++bit) {
            if (!bit_mark(reader(context, (uint16_t)(pos + 2u + bit * 2u)))) return 0;
            space = reader(context, (uint16_t)(pos + 3u + bit * 2u));
            byte_index = (uint16_t)(section * 6u + bit / 8u);
            raw[byte_index] = (uint8_t)(raw[byte_index] << 1);
            if (near_timing(space, 1645u)) raw[byte_index] |= 1u;
            else if (!near_timing(space, 610u)) return 0;
        }
        if (!bit_mark(reader(context, (uint16_t)(pos + 98u)))) return 0;
        if (section + 1u < sections &&
            !near_timing(reader(context, (uint16_t)(pos + 99u)), 5235u)) return 0;
    }
    if (!valid_bytes(raw, (uint16_t)(sections * 6u))) return 0;
    out->length = (uint16_t)(sections * 6u);
    out->carrier_khz = 38u;
    out->format = UI_SIGNAL_BOSCH144;
    for (i = 0u; i < out->length; ++i) out->timings[i] = raw[i];
    return 1;
}
void Bosch144_Init(Bosch144State *state) {
    /* Real 25.5 C prefix; 0x5B checksum REGENERATED, not captured.
     * Candidate protocol state only: not a hardware-confirmed full frame. */
    static const uint8_t seed[18] = {
        0xB2u,0x4Du,0xBFu,0x40u,0xC0u,0x3Fu,
        0xB2u,0x4Du,0xBFu,0x40u,0xC0u,0x3Fu,
        0xD5u,0x66u,0x20u,0x00u,0x00u,0x5Bu
    };
    if (state == 0) return;
    memcpy(state->raw, seed, sizeof(seed));
    state->power = 1u;
}
int Bosch144_StateFromSignal(Bosch144State *state, const UiSignal *signal) {
    uint16_t i;
    if (state == 0 || !Bosch144_ValidateSignal(signal)) return 0;
    if (signal->length == 18u)
        for (i = 0u; i < 18u; ++i) state->raw[i] = (uint8_t)signal->timings[i];
    state->power = (uint8_t)(signal->length == 18u);
    return 1;
}
uint8_t Bosch144_GetTempHalf(const Bosch144State *state) {
    uint8_t code;
    uint8_t i;
    if (state == 0 || (state->raw[15] & 1u) != 0u) return 0u;
    code = (uint8_t)(((state->raw[4] >> 4) << 2) |
           ((state->raw[15] >> 3) & 2u) | ((state->raw[14] >> 5) & 1u));
    for (i = 0u; i < 15u; ++i)
        if ((code & 0xFEu) == temp_map[i])
            return (uint8_t)(32u + i * 2u + (code & 1u));
    return 0u;
}
uint16_t Bosch144_DurationCount(const UiSignal *signal) {
    if (!Bosch144_ValidateSignal(signal)) return 0u;
    return (uint16_t)((signal->length / 6u) * 100u - 1u);
}
uint16_t Bosch144_Duration(const UiSignal *signal, uint16_t index) {
    uint16_t local;
    uint16_t bit;
    uint16_t byte_index;
    if (index >= Bosch144_DurationCount(signal)) return 0u;
    local = (uint16_t)(index % 100u);
    if (local == 0u) return 4366u;
    if (local == 1u) return 4415u;
    if (local == 99u) return 5235u;
    if ((local & 1u) == 0u) return 456u;
    bit = (uint16_t)((local - 3u) / 2u);
    byte_index = (uint16_t)((index / 100u) * 6u + bit / 8u);
    return (signal->timings[byte_index] & (1u << (7u - bit % 8u))) != 0u
           ? 1645u : 610u;
}
static void repair(uint8_t *raw) {
    uint16_t i;
    uint8_t sum;
    for (i = 0u; i < 6u; i += 2u) raw[i + 1u] = (uint8_t)(255u - raw[i]);
    memcpy(raw + 6, raw, 6u);
    sum = 0u;
    for (i = 12u; i < 17u; ++i) sum = (uint8_t)(sum + raw[i]);
    raw[17] = sum;
}
int Bosch144_Generate(const Bosch144State *state, UiSignal *out) {
    uint8_t raw[18];
    uint16_t i;
    uint16_t length;
    if (state == 0 || out == 0) return 0;
    length = state->power ? 18u : 12u;
    if (state->power) {
        memcpy(raw, state->raw, 18u);
        repair(raw);
        if (!valid_bytes(raw, 18u)) return 0;
    } else memcpy(raw, off_bytes, 12u);
    out->format = UI_SIGNAL_BOSCH144;
    out->carrier_khz = 38u;
    out->length = length;
    for (i = 0u; i < length; ++i) out->timings[i] = raw[i];
    return 1;
}
uint16_t Bosch144_GetFan(const Bosch144State *state) {
    if (state == 0) return 0u;
    return (uint16_t)(((uint16_t)(state->raw[2] >> 5) << 6) |
                      ((state->raw[13] >> 1) & 63u));
}
void Bosch144_SetTempHalf(Bosch144State *state, uint8_t half_celsius) {
    uint8_t code;
    if (state == 0) return;
    if (half_celsius < 32u) half_celsius = 32u;
    if (half_celsius > 60u) half_celsius = 60u;
    code = (uint8_t)(temp_map[(half_celsius - 32u) / 2u] | (half_celsius & 1u));
    state->raw[4] = (uint8_t)((state->raw[4] & 15u) | ((code >> 2) << 4));
    state->raw[14] = (uint8_t)((state->raw[14] & 0xDFu) | ((code & 1u) << 5));
    state->raw[15] = (uint8_t)((state->raw[15] & 0xEEu) | ((code & 2u) << 3));
    repair(state->raw);
}
void Bosch144_SetPower(Bosch144State *state, uint8_t power) {
    if (state != 0) state->power = (uint8_t)(power != 0u);
}
int Bosch144_SetFan(Bosch144State *state, uint16_t fan) {
    if (state == 0) return 0;
    if (fan != 371u && fan != 51u && fan != 458u && fan != 276u &&
        fan != 158u && fan != 104u && fan != 114u) return 0;
    state->raw[2] = (uint8_t)((state->raw[2] & 31u) | ((fan >> 6) << 5));
    state->raw[13] = (uint8_t)((state->raw[13] & 0x81u) | ((fan & 63u) << 1));
    repair(state->raw);
    return 1;
}
int Bosch144_SetMode(Bosch144State *state, uint8_t mode) {
    if (state == 0) return 0;
    if (mode != 0u && mode != 3u && mode != 5u && mode != 6u && mode != 2u) return 0;
    state->raw[4] = (uint8_t)((state->raw[4] & 0xF3u) | ((mode >> 1) << 2));
    state->raw[13] = (uint8_t)((state->raw[13] & 0xFEu) | (mode & 1u));
    if (mode == 3u || mode == 5u) (void)Bosch144_SetFan(state, 51u);
    repair(state->raw);
    return 1;
}
int Bosch144_Button(Bosch144State *state, uint8_t key) {
    static const uint8_t modes[5] = {5u, 0u, 3u, 6u, 2u};
    static const uint16_t fans[6] = {371u, 458u, 276u, 158u, 104u, 114u};
    uint8_t temp;
    uint8_t i;
    uint16_t fan;
    if (state == 0 || key < 1u || key > 5u) return 0;
    if (key == 1u || key == 2u) {
        temp = Bosch144_GetTempHalf(state);
        if (temp == 0u) return 0;
        if (key == 1u && temp < 60u) ++temp;
        if (key == 2u && temp > 32u) --temp;
        Bosch144_SetTempHalf(state, temp);
    } else if (key == 3u) {
        temp = Bosch144_GetMode(state);
        for (i = 0u; i < 5u; ++i) if (modes[i] == temp) break;
        (void)Bosch144_SetMode(state, modes[(i + 1u) % 5u]);
    } else if (key == 4u) {
        fan = Bosch144_GetFan(state);
        if (fan == 51u) fan = 371u;
        for (i = 0u; i < 6u; ++i) if (fans[i] == fan) break;
        (void)Bosch144_SetFan(state, fans[(i + 1u) % 6u]);
    } else state->power = (uint8_t)!state->power;
    return 1;
}
uint8_t Bosch144_GetMode(const Bosch144State *state) {
    if (state == 0) return 255u;
    return (uint8_t)(((state->raw[4] >> 1) & 6u) | (state->raw[13] & 1u));
}
