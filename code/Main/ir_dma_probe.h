#ifndef IR_DMA_PROBE_H
#define IR_DMA_PROBE_H
#include <stdint.h>

#define IR_DMA_PROBE_CAPACITY 512u
#define IR_DMA_PROBE_FULL_FALL 1u
#define IR_DMA_PROBE_FULL_RISE 2u
#define IR_DMA_PROBE_OVERCAPTURE 4u
#define IR_DMA_PROBE_DMA_ERROR 8u
#define IR_DMA_PROBE_DRAIN_TIMEOUT 16u
#define IR_DMA_PROBE_INVALID 32u
#define IR_DMA_PROBE_OUTPUT_SHORT 64u
#define IR_DMA_PROBE_ZERO_INTERVAL 128u

typedef struct {
    uint16_t fall_count;
    uint16_t rise_count;
    uint16_t end_timer;
    uint16_t timer_status;
    uint32_t dma_status;
    uint32_t flags;
    uint8_t running;
    uint16_t fall[IR_DMA_PROBE_CAPACITY];
    uint16_t rise[IR_DMA_PROBE_CAPACITY];
} IrDmaProbe;

extern volatile IrDmaProbe g_ir_dma_probe;

/* Foreground, serialized with RX/TX transitions. End is idempotent.
 * Begin owns TIM4, DMA1 channels 1/4 and forces PB6 push-pull LOW.
 * PB7 must already be an input; no EXTI, TIM2 or NVIC changes are made.
 * End keeps PB6 LOW and retains arrays until next Begin overwrites them.
 * TX adapter MUST restore PSC=0, OC1 output/polarity, AF-PP PB6 and PWM.
 * Counts/flags are frozen at End (not live DMA producer indices).
 * Call End on original collector READY as well as RX disable.
 */
void IrDmaProbe_Begin(void);
void IrDmaProbe_End(void);

/* Diagnostic only: alternate edges in acquisition order, never sort raw
 * 16-bit timestamps. first_rising=0 means active-low IR starts with a fall.
 * Each adjacent interval MUST be 1..65535 us; whole wraps between edges
 * and missing balanced edge pairs cannot be inferred from these samples.
 * Produces N-1 durations (no idle prefix or invented trailing duration).
 * Returns flags; writes produced count. FULL means possibly truncated,
 * including exactly-full captures. Input flags should include End's flags.
 * Reconstruct only stopped buffers; output must not alias either input.
 */
uint32_t IrDmaProbe_Reconstruct(const volatile uint16_t *fall,
    uint16_t fall_count, const volatile uint16_t *rise, uint16_t rise_count,
    uint8_t first_rising, uint32_t input_flags, uint16_t *durations,
    uint16_t capacity, uint16_t *produced);
#endif
