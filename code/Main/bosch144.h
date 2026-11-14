/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Adapted from IRremoteESP8266; Copyright 2022 Nico Thien / David Conran.
 * Modified portable API; see third_party/BOSCH144-NOTICE.txt. */
#ifndef BOSCH144_H
#define BOSCH144_H
#include <stdint.h>
#include "ui.h"
#define BOSCH144_MODE_COOL 0u
#define BOSCH144_MODE_DRY 3u
#define BOSCH144_MODE_AUTO 5u
#define BOSCH144_MODE_HEAT 6u
#define BOSCH144_MODE_FAN 2u
#define BOSCH144_FAN_AUTO 371u
#define BOSCH144_FAN_20 458u
#define BOSCH144_FAN_40 276u
#define BOSCH144_FAN_60 158u
#define BOSCH144_FAN_80 104u
#define BOSCH144_FAN_100 114u
typedef struct { uint8_t raw[18]; uint8_t power; } Bosch144State;
typedef uint16_t (*Bosch144TimingReader)(void *context, uint16_t index);
int Bosch144_ValidateSignal(const UiSignal *signal);
int Bosch144_Decode(Bosch144TimingReader reader, void *context,
                    uint16_t count, UiSignal *out);
uint16_t Bosch144_DurationCount(const UiSignal *signal);
/* Index is mark-first, mark-last; invalid signal/index returns zero.
 * 299 durations on, 199 off, microseconds; carrier is nominal 38 kHz. */
uint16_t Bosch144_Duration(const UiSignal *signal, uint16_t index);
/* Init is the real 25.5 C COOL/AUTO prefix with REGENERATED checksum
 * 0x5B: candidate only, NOT a fully captured or hardware-verified frame. */
void Bosch144_Init(Bosch144State *state);
/* Off imports only power, preserving the caller's prior ON state.
 * Initialize state before importing an OFF frame. Failed imports unchanged. */
int Bosch144_StateFromSignal(Bosch144State *state, const UiSignal *signal);
int Bosch144_Generate(const Bosch144State *state, UiSignal *out);
uint8_t Bosch144_GetTempHalf(const Bosch144State *state);
uint8_t Bosch144_GetMode(const Bosch144State *state);
uint16_t Bosch144_GetFan(const Bosch144State *state);
void Bosch144_SetTempHalf(Bosch144State *state, uint8_t half_celsius);
int Bosch144_SetMode(Bosch144State *state, uint8_t mode);
int Bosch144_SetFan(Bosch144State *state, uint16_t fan);
void Bosch144_SetPower(Bosch144State *state, uint8_t power);
/* Mode cycle AUTO COOL DRY HEAT FAN; fan AUTO 20 40 60 80 100.
 * Entering AUTO/DRY selects upstream's special auto fan code 51.
 * SetTempHalf clamps 32..60; zero getter means unknown/Fahrenheit. */
int Bosch144_Button(Bosch144State *state, uint8_t key);
#endif
