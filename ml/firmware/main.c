#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include <stdio.h>
#include "gesture_recognizer.h"
#include "gesture_model_v1.h"

#define MPU_ADDRESS (0x68u << 1) /* AD0 grounded */
#define SAMPLE_MS 10u
#define I2C_TIMEOUT_MS 5u
#define SCL GPIO_Pin_12
#define SDA GPIO_Pin_14
#define BT6 GPIO_Pin_15
#define MPU6050_ID 0x68u
#define MPU6500_ID 0x70u
#define BT6_DEBOUNCE_MS 20u
#define WINDOW_SAMPLES 150u
#define WINDOW_MIN_MS 150u
#define WINDOW_MS (WINDOW_SAMPLES * SAMPLE_MS)

static volatile uint32_t ticks;
static volatile uint8_t button_pressed;
static volatile uint8_t button_raw;
static volatile uint32_t button_stable_at;
static uint32_t button_changed_at;
static uint32_t window_started_at;
static uint32_t window_start_head;

enum { BT6_READY, BT6_RECORDING, BT6_WAIT_RELEASE };

/* SWD reads this layout using the symbol address in the Keil map. */
#define SWD_CAPACITY 256u
enum { MODE_CAPTURE = 1, MODE_RECOGNIZE };
#define DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

/* SWD snapshot. Odd sequence means the result or its raw rows are changing. */
volatile struct {
    uint32_t version, mode, request, sequence;
    uint32_t number, flags, count, duration_ms, cycles;
    float angle_deg, scores[3];
    uint32_t label, who;
} g_recognition;

volatile struct {
    uint32_t number;
    uint32_t count;
    uint32_t who;
    uint32_t ready;
    uint32_t ack;
    uint32_t duration_ms;
    uint32_t truncated;
    SwdSample samples[WINDOW_SAMPLES];
} g_pending_window;

static unsigned uart_window_index;
static uint32_t uart_ack;
static uint8_t uart_ack_digits;
static uint8_t uart_ack_active;

volatile struct {
    uint32_t magic;
    uint32_t head;
    uint32_t who_am_i;
    uint32_t status;
    uint32_t button_state;
    uint32_t window_end;
    uint32_t windows_completed;
    uint32_t windows_cancelled;
    SwdSample samples[SWD_CAPACITY];
} g_swd_capture;

void SysTick_Handler(void)
{
    uint8_t pressed = !GPIO_ReadInputDataBit(GPIOB, BT6);
    ++ticks;
    if (pressed != button_raw) {
        button_raw = pressed;
        button_changed_at = ticks;
    }
    if (button_pressed != button_raw && ticks - button_changed_at >= BT6_DEBOUNCE_MS) {
        button_stable_at = ticks;
        button_pressed = button_raw;
    }
}

static int send_window_row(void);

static void release_acknowledged_window(void)
{
    if (g_pending_window.ready && g_pending_window.ack == g_pending_window.number)
        g_pending_window.ready = 0;
}

static void retry_delay(void)
{
    unsigned i;
    for (i = 0; i < 100; ++i) {
        uint32_t start = ticks;
        release_acknowledged_window();
        send_window_row();
        while (ticks - start < SAMPLE_MS) {}
    }
}

static void delay_ms(uint32_t delay)
{
    uint32_t start = ticks;
    while (ticks - start < delay) {}
}

static void uart_write(const char *text)
{
    while (*text) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {}
        USART_SendData(USART1, (uint8_t)*text++);
    }
}

static void init_pins_and_uart(void)
{
    GPIO_InitTypeDef pins;
    USART_InitTypeDef uart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                          RCC_APB2Periph_USART1, ENABLE);
    GPIO_SetBits(GPIOB, SCL | SDA);
    pins.GPIO_Speed = GPIO_Speed_2MHz;
    pins.GPIO_Pin = SCL | SDA;
    pins.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(GPIOB, &pins);

    pins.GPIO_Pin = GPIO_Pin_9;
    pins.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &pins);
    /* Optional USB-TTL TXD for standalone UART acknowledgement. */
    pins.GPIO_Pin = GPIO_Pin_10;
    pins.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &pins);
    pins.GPIO_Pin = BT6;
    pins.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &pins);
    USART_StructInit(&uart);
    uart.USART_BaudRate = 115200;
    uart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART1_IRQn);
}

/* SysTick is already a free-running core-clock counter. */
static void bus_delay(void)
{
    uint32_t previous = SysTick->VAL;
    uint32_t remaining = SystemCoreClock / 200000u; /* 5 us */
    uint32_t period = SysTick->LOAD + 1u;
    for (;;) {
        uint32_t now = SysTick->VAL;
        uint32_t elapsed = previous >= now ? previous - now : previous + period - now;
        if (elapsed >= remaining) return;
        remaining -= elapsed;
        previous = now;
    }
}

static int clock_high(void)
{
    uint32_t start = ticks;
    GPIO_SetBits(GPIOB, SCL);
    while (!GPIO_ReadInputDataBit(GPIOB, SCL)) {
        if (ticks - start >= I2C_TIMEOUT_MS) return 0;
    }
    bus_delay();
    return 1;
}

static int release_sda(void)
{
    uint32_t start = ticks;
    GPIO_SetBits(GPIOB, SDA);
    while (!GPIO_ReadInputDataBit(GPIOB, SDA)) {
        if (ticks - start >= I2C_TIMEOUT_MS) return 0;
    }
    bus_delay();
    return 1;
}

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

/* Probe the alternate AD0 address without configuring any device there. */
static int alternate_address_ack(void)
{
    int ack = start_bus() && send_byte(0x69u << 1);
    return stop_bus() && ack;
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
    if (!stop_bus() || !ok) {
        g_swd_capture.status = error ? error : 11;
        return 0;
    }
    return 1;
}

static int configure_register(uint8_t reg, uint8_t value)
{
    uint8_t actual;
    return write_register(reg, value) &&
           read_registers(reg, &actual, 1) && actual == value;
}

static int configure_sensor(uint8_t id)
{
    /* Both devices use CONFIG=4 for the gyro 20 Hz DLPF and 1 kHz base
     * rate. GYRO_CONFIG=0 also selects the DLPF path on the MPU6500. */
    if (!configure_register(0x1A, 4) ||
        !configure_register(0x1B, 0) ||
        !configure_register(0x1C, 0)) return 0;

    /* The MPU6050 filters acceleration through CONFIG. The MPU6500
     * has a separate anti-alias filter; 0x04 selects its 20 Hz path. */
    if (id == MPU6500_ID &&
        (!configure_register(0x6C, 0) ||
         !configure_register(0x1D, 4))) return 0;

    /* SMPLRT_DIV: 1000 / (1 + 9) = 100 samples per second. */
    return configure_register(0x19, 9);
}

static int init_sensor(void)
{
    uint8_t id;
    char message[48];

    if (!read_registers(0x75, &id, 1)) {
        if (g_swd_capture.status == 7 && alternate_address_ack())
            g_swd_capture.status = 12;
        uart_write("# ERROR I2C WHO_AM_I read failed (address 0x68)\r\n");
        return 0;
    }
    g_swd_capture.who_am_i = id;
    if (id != MPU6050_ID && id != MPU6500_ID) {
        g_swd_capture.status = 2;
        sprintf(message, "# ERROR WHO_AM_I=0x%02X, expected 0x68/0x70\r\n", (unsigned)id);
        uart_write(message);
        return 0;
    }
    if (!write_register(0x6B, 0x01)) {
        g_swd_capture.status = 3;
        uart_write("# ERROR I2C sensor wake failed\r\n");
        return 0;
    }
    delay_ms(100);
    if (!configure_sensor(id)) {
        g_swd_capture.status = 4;
        uart_write("# ERROR I2C sensor configuration failed\r\n");
        return 0;
    }
    g_swd_capture.status = 0;
    uart_write(id == MPU6500_ID ? "# OK MPU6500 WHO_AM_I=0x70\r\n" :
                                 "# OK MPU6050 WHO_AM_I=0x68\r\n");
    uart_write("t_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\r\n");
    return 1;
}

static int16_t read_axis(const uint8_t *bytes)
{
    return (int16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void publish_sample(uint32_t time, const uint8_t *raw)
{
    uint32_t head = g_swd_capture.head;
    volatile SwdSample *sample = &g_swd_capture.samples[head % SWD_CAPACITY];
    unsigned i;

    sample->t_ms = time;
    for (i = 0; i < 3; ++i) {
        sample->axes[i] = read_axis(raw + 2u * i);
        sample->axes[i + 3u] = read_axis(raw + 8u + 2u * i);
    }
    g_swd_capture.head = head + 1u;
}

void USART1_IRQHandler(void)
{
    if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(USART1);
        if (byte == 'A') {
            uart_ack = 0;
            uart_ack_digits = 0;
            uart_ack_active = 1;
        } else if (uart_ack_active && byte >= '0' && byte <= '9' &&
                   uart_ack_digits < 10 &&
                   uart_ack <= (0xFFFFFFFFu - (byte - '0')) / 10u) {
            uart_ack = uart_ack * 10u + (byte - '0');
            ++uart_ack_digits;
        } else {
            if (byte == '\n' && uart_ack_active && uart_ack_digits &&
                g_pending_window.ready && uart_ack == g_pending_window.number)
                g_pending_window.ack = uart_ack;
            uart_ack_active = 0;
        }
    }
}

static void recognize_window(unsigned count, uint32_t duration, uint32_t truncated)
{
    unsigned i;
    uint32_t start_cycles;
    GesturePrediction prediction;

    ++g_recognition.sequence;
    for (i = 0; i < count; ++i)
        g_pending_window.samples[i] =
            g_swd_capture.samples[(window_start_head + i) % SWD_CAPACITY];
    start_cycles = DWT_CYCCNT;
    prediction = gesture_predict(g_pending_window.samples, count, duration, truncated);
    g_recognition.cycles = DWT_CYCCNT - start_cycles;
    g_recognition.flags = prediction.flags;
    g_recognition.count = count;
    g_recognition.duration_ms = duration;
    g_recognition.angle_deg = prediction.angle_deg;
    for (i = 0; i < 3; ++i) g_recognition.scores[i] = prediction.scores[i];
    g_recognition.label = prediction.label;
    g_recognition.who = g_swd_capture.who_am_i;
    ++g_recognition.number;
    ++g_recognition.sequence;
}

static void finish_window(uint32_t duration, uint32_t truncated)
{
    unsigned count = g_swd_capture.head - window_start_head;
    unsigned i;
    if (count > WINDOW_SAMPLES) count = WINDOW_SAMPLES;
    while (count && g_swd_capture.samples[(window_start_head + count - 1u) % SWD_CAPACITY].t_ms -
                    window_started_at >= duration)
        --count;
    if (duration < WINDOW_MIN_MS || !count)
        ++g_swd_capture.windows_cancelled;
    if (g_recognition.mode == MODE_RECOGNIZE) {
        recognize_window(count, duration, truncated);
        return;
    }
    if (duration < WINDOW_MIN_MS || !count) return;
    for (i = 0; i < count; ++i)
        g_pending_window.samples[i] =
            g_swd_capture.samples[(window_start_head + i) % SWD_CAPACITY];
    g_pending_window.number = g_swd_capture.windows_completed + 1u;
    g_pending_window.count = count;
    g_pending_window.who = g_swd_capture.who_am_i;
    g_pending_window.ack = 0;
    g_pending_window.duration_ms = duration;
    g_pending_window.truncated = truncated;
    uart_window_index = 0;
    g_swd_capture.window_end = window_start_head + count;
    ++g_swd_capture.windows_completed;
    g_pending_window.ready = 1;
}

static void service_bt6(uint32_t now)
{
    release_acknowledged_window();

    if (g_recognition.request && !button_raw && !button_pressed &&
        !g_pending_window.ready && g_swd_capture.button_state == BT6_READY) {
        uint32_t request = g_recognition.request;
        if ((request == MODE_CAPTURE || request == MODE_RECOGNIZE) &&
            request != g_recognition.mode) {
            ++g_recognition.sequence;
            g_recognition.number = 0;
            g_recognition.mode = request;
            ++g_recognition.sequence;
        }
        g_recognition.request = 0;
    }

    if (g_swd_capture.button_state == BT6_WAIT_RELEASE) {
        if (!button_raw && !button_pressed)
            g_swd_capture.button_state = BT6_READY;
    } else if (g_swd_capture.button_state == BT6_READY) {
        if (button_pressed && g_pending_window.ready) {
            g_swd_capture.button_state = BT6_WAIT_RELEASE;
        } else if (button_pressed) {
            window_started_at = button_stable_at;
            window_start_head = g_swd_capture.head;
            g_swd_capture.button_state = BT6_RECORDING;
        }
    } else if (!button_pressed) {
        uint32_t duration = button_stable_at - window_started_at;
        finish_window(duration > WINDOW_MS ? WINDOW_MS : duration, duration > WINDOW_MS);
        g_swd_capture.button_state = BT6_WAIT_RELEASE;
    } else if ((int32_t)(now - window_started_at) >= (int32_t)WINDOW_MS) {
        finish_window(WINDOW_MS, 1);
        g_swd_capture.button_state = BT6_WAIT_RELEASE;
    }
}

static int send_window_row(void)
{
    char line[112];
    SwdSample sample;
    unsigned index;
    unsigned checksum = 0;
    unsigned i;
    if (!g_pending_window.ready) return 0;
    index = uart_window_index;
    sample = g_pending_window.samples[index];
    /* CRC-16/CCITT over the exact ASCII prefix. */
    sprintf(line, "W4,%lu,%lu,%lu,%lu,%lu,%u,%lu,%d,%d,%d,%d,%d,%d",
            (unsigned long)g_pending_window.number,
            (unsigned long)g_pending_window.count,
            (unsigned long)g_pending_window.who,
            (unsigned long)g_pending_window.duration_ms,
            (unsigned long)g_pending_window.truncated, index,
            (unsigned long)sample.t_ms, sample.axes[0], sample.axes[1],
            sample.axes[2], sample.axes[3], sample.axes[4], sample.axes[5]);
    for (i = 0; line[i]; ++i) {
        unsigned bit;
        checksum ^= (unsigned)line[i] << 8;
        for (bit = 0; bit < 8; ++bit)
            checksum = ((checksum << 1) ^ ((checksum & 0x8000u) ? 0x1021u : 0u)) & 0xFFFFu;
    }
    sprintf(line + i, ",%04X\r\n", checksum);
    uart_write(line);
    uart_window_index = (index + 1u) % g_pending_window.count;
    return 1;
}

static void send_sample(uint32_t time, const uint8_t *raw)
{
    char line[128];

    sprintf(line, "%lu,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f\r\n",
            (unsigned long)time, read_axis(raw) / 16384.0,
            read_axis(raw + 2) / 16384.0, read_axis(raw + 4) / 16384.0,
            read_axis(raw + 8) / 131.0, read_axis(raw + 10) / 131.0,
            read_axis(raw + 12) / 131.0);
    uart_write(line);
}

int main(void)
{
    uint8_t raw[14];
    uint32_t last;
    uint32_t now;

    SystemCoreClockUpdate();
    init_pins_and_uart();
    g_recognition.version = GESTURE_MODEL_VERSION;
    g_recognition.mode = GPIO_ReadInputDataBit(GPIOB, BT6) ? MODE_CAPTURE : MODE_RECOGNIZE;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
    g_swd_capture.button_state = BT6_WAIT_RELEASE;
    g_swd_capture.magic = 0x47535734u;
    if (SysTick_Config(SystemCoreClock / 1000u)) {
        uart_write("# ERROR SysTick configuration failed\r\n");
        for (;;) {}
    }

    delay_ms(100);
    for (;;) {
        while (!init_sensor()) retry_delay();
        last = ticks;
        for (;;) {
            do {
                now = ticks;
                service_bt6(now);
                now = ticks;
            } while (now - last < SAMPLE_MS);
            last = now;
            if (!read_registers(0x3B, raw, sizeof(raw))) {
                if (g_swd_capture.button_state == BT6_RECORDING)
                    ++g_swd_capture.windows_cancelled;
                g_swd_capture.button_state = BT6_WAIT_RELEASE;
                uart_write("# ERROR I2C six-axis read failed; retrying sensor setup\r\n");
                retry_delay();
                break;
            }
            publish_sample(now, raw);
            if (!send_window_row())
                send_sample(now, raw);
        }
    }
}
