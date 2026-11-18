#include "gesture_shadow.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "gesture_recognizer.h"
#include "gesture_model_v1.h"
#include <string.h>

#define MPU_ADDRESS 0xD0u
#define SCL GPIO_Pin_12
#define SDA GPIO_Pin_14
#define BT6 GPIO_Pin_15
#define CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define CYCLE_CONTROL (*(volatile uint32_t *)0xE0001000u)
#define CONTROL_PROTOCOL 0x53484432u
#define WINDOW_CAP 150u
#define SENSOR_FAULT 32u
#define CLOCK_GAP 64u

enum { RELEASE, IDLE, RECORDING };

/* Foreground owns the result; SWD reads a stable, even sequence twice.
   proposed_key records the key returned for this window, not IR TX success. */
volatile struct {
    uint32_t protocol, model, sequence, number;
    uint32_t who, sensor_state, button_state, flags;
    uint32_t count, duration_ms, proposed_key, label;
    uint32_t min_gap_us, max_gap_us, missed_periods, proposals;
    float angle_deg, scores[3];
} g_gesture_shadow;

static GestureAccumulator window;
static volatile uint32_t edge_ms, edge_cycles;
static volatile uint8_t raw, pressed;
static uint32_t raw_since;
static uint32_t cycles_per_us, next_setup, started, started_cycles, last_cycle, next_sample;
static uint32_t min_gap, max_gap, missed;
static uint8_t setup_step;

static void bus_delay(void)
{
    uint32_t start = CYCCNT;
    while (CYCCNT - start < 5u * cycles_per_us) {}
}

static int release_pin(uint16_t pin)
{
    uint32_t start = CYCCNT;
    GPIO_SetBits(GPIOB, pin);
    while (!GPIO_ReadInputDataBit(GPIOB, pin)) {
        if (CYCCNT - start >= 100u * cycles_per_us) return 0;
    }
    bus_delay();
    return 1;
}

static int clock_high(void) { return release_pin(SCL); }
static int release_sda(void) { return release_pin(SDA); }

static int start_bus(void)
{
    if (!release_sda() || !clock_high() || !GPIO_ReadInputDataBit(GPIOB, SDA)) return 0;
    GPIO_ResetBits(GPIOB, SDA);
    bus_delay();
    GPIO_ResetBits(GPIOB, SCL);
    return 1;
}

static int stop_bus(void)
{
    int ok;
    GPIO_ResetBits(GPIOB, SCL | SDA);
    bus_delay();
    ok = clock_high();
    return release_sda() && ok;
}

/* Exchange one bit; releasing SDA allows the device to drive it low. */
static int bus_bit(int released)
{
    int value;
    GPIO_WriteBit(GPIOB, SDA, released ? Bit_SET : Bit_RESET);
    bus_delay();
    if (!clock_high()) return -1;
    value = GPIO_ReadInputDataBit(GPIOB, SDA);
    GPIO_ResetBits(GPIOB, SCL);
    return value;
}

static int send_byte(uint8_t byte)
{
    unsigned bit;
    for (bit = 0; bit < 8; ++bit) {
        if (bus_bit(byte & 0x80u) < 0) return 0;
        byte <<= 1;
    }
    return bus_bit(1) == 0;
}

static int select_register(uint8_t reg)
{
    return start_bus() && send_byte(MPU_ADDRESS) && send_byte(reg);
}

static int write_register(uint8_t reg, uint8_t value)
{
    int ok = select_register(reg) && send_byte(value);
    return stop_bus() && ok;
}

static int read_registers(uint8_t reg, uint8_t *out, unsigned length)
{
    uint32_t error = 0;
    int ok;

    if (!start_bus()) error = 6;
    else if (!send_byte(MPU_ADDRESS)) error = 7;
    else if (!send_byte(reg)) error = 8;
    else if (!stop_bus() || !start_bus()) error = 9;
    else if (!send_byte(MPU_ADDRESS | 1u)) error = 10;
    ok = error == 0;
    while (ok && length != 0u) {
        unsigned bit;
        uint8_t byte = 0;
        for (bit = 0; bit < 8; ++bit) {
            int value = bus_bit(1);
            if (value < 0) {
                ok = 0;
                break;
            }
            byte = (uint8_t)((byte << 1) | value);
        }
        if (ok) {
            *out++ = byte;
            --length;
            ok = bus_bit(length == 0u) >= 0;
        }
    }
    return stop_bus() && ok;
}

static int configure_register(uint8_t reg, uint8_t value)
{
    uint8_t actual;
    return write_register(reg, value) &&
           read_registers(reg, &actual, 1) && actual == value;
}


static int16_t axis_value(const uint8_t *raw_bytes)
{
    return (int16_t)(((uint16_t)raw_bytes[0] << 8) | raw_bytes[1]);
}

static void sensor_poll(uint32_t now)
{
    static const uint8_t regs[][2] = {{0x1A,4},{0x1B,0},{0x1C,0},{0x6C,0},{0x1D,4},{0x19,9}};
    uint8_t id;
    unsigned index;
    if ((int32_t)(now - next_setup) < 0) return;
    if (!setup_step) {
        if (!read_registers(0x75, &id, 1)) goto fault;
        g_gesture_shadow.who = id;
        if (id != 0x70 || !write_register(0x6B, 1)) goto fault;
        g_gesture_shadow.sensor_state = 1;
        next_setup = now + 100u;
    } else {
        index = setup_step - 1u;
        if (!configure_register(regs[index][0], regs[index][1])) goto fault;
        if (index + 1u == sizeof(regs) / sizeof(regs[0])) {
            g_gesture_shadow.sensor_state = 2;
            return;
        }
        next_setup = now + 1u;
    }
    ++setup_step;
    return;
fault:
    setup_step = 0;
    g_gesture_shadow.sensor_state = 3;
    next_setup = now + 1000u;
}

void GestureShadow_Init(void)
{
    GPIO_InitTypeDef pins;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_SetBits(GPIOB, SCL | SDA);
    pins.GPIO_Pin = SCL | SDA;
    pins.GPIO_Mode = GPIO_Mode_Out_OD;
    pins.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &pins);
    pins.GPIO_Pin = BT6;
    pins.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &pins);
    cycles_per_us = SystemCoreClock / 1000000u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    CYCLE_CONTROL |= 1u;
    g_gesture_shadow.button_state = RELEASE;
    g_gesture_shadow.model = GESTURE_MODEL_VERSION;
    g_gesture_shadow.protocol = CONTROL_PROTOCOL;
}

void GestureShadow_Tick(uint32_t now)
{
    uint8_t value = !GPIO_ReadInputDataBit(GPIOB, BT6);
    if (value != raw) { raw = value; raw_since = now; }
    if (value != pressed && now - raw_since >= 20u) {
        edge_ms = now;
        edge_cycles = CYCCNT;
        pressed = value;
    }
}

static uint8_t publish(uint32_t duration, unsigned truncated, unsigned extra_flags, unsigned allow)
{
    GesturePrediction prediction = gesture_finish(&window, duration, truncated);
    uint32_t flags = prediction.flags | extra_flags;
    unsigned i;
    unsigned key = 0;
    if (!flags && allow) {
        if (prediction.label == GESTURE_CW) key = 1;
        else if (prediction.label == GESTURE_CCW) key = 2;
    }
    ++g_gesture_shadow.sequence;
    g_gesture_shadow.flags = flags;
    g_gesture_shadow.count = window.count;
    g_gesture_shadow.duration_ms = duration;
    g_gesture_shadow.proposed_key = key;
    g_gesture_shadow.label = flags ? GESTURE_REJECT : prediction.label;
    g_gesture_shadow.min_gap_us = window.count > 1u ? min_gap : 0;
    g_gesture_shadow.max_gap_us = max_gap;
    g_gesture_shadow.missed_periods = missed;
    g_gesture_shadow.proposals += key != 0u;
    g_gesture_shadow.angle_deg = prediction.angle_deg;
    for (i = 0; i < 3; ++i) g_gesture_shadow.scores[i] = prediction.scores[i];
    ++g_gesture_shadow.number;
    ++g_gesture_shadow.sequence;
    return (uint8_t)key;
}

uint8_t GestureShadow_Poll(uint32_t now, unsigned allow_proposal)
{
    uint32_t duration, cycles, gap, late;
    uint8_t bytes[14];
    SwdSample sample;
    unsigned i;

    if (g_gesture_shadow.sensor_state != 2u) {
        sensor_poll(now);
        return 0;
    }
    if (g_gesture_shadow.button_state == RELEASE) {
        if (!raw && !pressed) g_gesture_shadow.button_state = IDLE;
        return 0;
    }
    if (g_gesture_shadow.button_state == IDLE) {
        if (!pressed) return 0;
        started = edge_ms;
        started_cycles = edge_cycles;
        next_sample = started;
        memset(&window, 0, sizeof(window));
        min_gap = 0xFFFFFFFFu;
        max_gap = missed = 0;
        g_gesture_shadow.button_state = RECORDING;
    }
    duration = (pressed ? now : edge_ms) - started;
    /* SysTick can confirm a new press between the caller's now and this poll. */
    if ((int32_t)duration < 0) return 0;
    if (!pressed || duration >= 1500u) {
        uint32_t end = pressed ? CYCCNT : edge_cycles;
        uint32_t wall_ms = (end - started_cycles) / (cycles_per_us * 1000u);
        unsigned clock_gap = missed || (window.count > 1u && (min_gap < 9000u || max_gap > 11000u)) ||
            wall_ms > duration + 2u || duration > wall_ms + 2u ||
            (window.count && (end - last_cycle) / cycles_per_us > 11000u &&
             (int32_t)(end - last_cycle) > 0);
        g_gesture_shadow.button_state = RELEASE;
        return publish(duration > 1500u ? 1500u : duration,
                       duration > 1500u || pressed, clock_gap ? CLOCK_GAP : 0u, allow_proposal);
    }
    if ((int32_t)(now - next_sample) < 0 || window.count == WINDOW_CAP) return 0;
    late = now - next_sample;
    missed += late / 10u;
    next_sample += (late / 10u + 1u) * 10u;
    cycles = CYCCNT;
    if (window.count) {
        gap = (cycles - last_cycle) / cycles_per_us;
        if (gap < min_gap) min_gap = gap;
        if (gap > max_gap) max_gap = gap;
    }
    if (!read_registers(0x3B, bytes, sizeof(bytes))) {
        publish(duration, 0, SENSOR_FAULT, 0);
        g_gesture_shadow.button_state = RELEASE;
        g_gesture_shadow.sensor_state = 3;
        setup_step = 0;
        next_setup = now + 1000u;
        return 0;
    }
    last_cycle = cycles;
    sample.t_ms = now;
    for (i = 0; i < 3; ++i) {
        sample.axes[i] = axis_value(bytes + 2u * i);
        sample.axes[i+3u] = axis_value(bytes + 8u + 2u * i);
    }
    gesture_add(&window, &sample);
    return 0;
}
