#ifndef IR_HARDWARE_H
#define IR_HARDWARE_H
#include "ui.h"
/* Owns PB7/TIM4_TI2, PB6/TIM4_CH1, DMA1 channels 1/4, TIM2/TIM3.
 * EXTI7 delivery stays disabled; RX DMA and TX exclusively share TIM4.
 * 72 MHz timer clocks (APB1 prescaler 2); no remapping of TIM4.
 * All application calls are foreground-only. No OLED/UI work in IRQs.
 * Init leaves learning DISABLED. Enable only on entering receiving;
 * disable before leaving receiving or doing Flash operations. A change
 * discards partial/completed captures, so old-page data cannot leak.
 * Call Poll regularly (~1ms). RX is muted throughout TX and for 30ms
 * after TX, enable, or a successful TryReceive (discard an in-flight repeat).
 * During that settling interval, a sampled LOW restarts the quiet timer.
 * RAW timings are microseconds, first/last MARK, alternating MARK/SPACE.
 * RAW has at most 255 durations; BOSCH144 uses strict full-frame decoding
 * into compact bytes and streams 299 ON / 199 OFF durations when sent.
 * 38 kHz on learned signals is nominal, not a measured carrier.
 * DMA 16-bit timestamps cannot identify whole wraps between adjacent edges;
 * sample Poll regularly. Frozen invalid data is retained for diagnosis.
 * Send copies the signal before returning; caller may mutate afterwards.
 * Busy reports transmission only (not receiver settling). */
void IrHardware_Init(void);
void IrHardware_Poll(uint32_t now_ms);
void IrHardware_SetReceiveEnabled(uint8_t enabled);
/* 1: full validated/captured signal; 0: pending; -1: invalid, frozen.
 * On -1 dest is unchanged and repeated calls return -1. DMA data remains
 * frozen until explicit disable/re-enable (or Send). Never treat -1 as true. */
int IrHardware_TryReceive(UiSignal *dest);
#define IR_RX_ERROR_DMA       0x100u
#define IR_RX_ERROR_COUNTS    0x200u
#define IR_RX_ERROR_INTERVAL  0x400u
#define IR_RX_ERROR_CAPACITY  0x800u
#define IR_RX_ERROR_PROTOCOL  0x1000u
#define IR_RX_ERROR_TIMEOUT   0x2000u
/* Low eight bits retain ir_dma_probe.h flags. Cleared on enable transitions
 * or an accepted Send (which explicitly takes ownership away from RX). */
uint32_t IrHardware_LastReceiveError(void);
int IrHardware_Send(const UiSignal *signal);
int IrHardware_Busy(void);
#endif
