#include "ir_dma_probe.h"

volatile IrDmaProbe g_ir_dma_probe;

#ifndef IR_DMA_PROBE_PORTABLE
#include "stm32f10x.h"

/* RM0008 Rev21 section 13.3.7, Table 78 (DMA1 request mapping):
 * TIM4_CH1 -> DMA1 channel 1; TIM4_CH2 -> DMA1 channel 4.
 * General-purpose TIMx_CCMR1: CC1S=10 selects TI2 indirectly;
 * CC2S=01 selects TI2 directly. CC1P=1 falling, CC2P=0 rising.
 * Neither reset slave mode nor capture prescalers/filters are used.
 */
void IrDmaProbe_Begin(void) {
    uint32_t dma_config;
    IrDmaProbe_End();
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    (void)RCC->AHBENR;
    /* Set latch first, then GPIO mode: CH1 is about to become an input,
     * so forced-inactive OC1 alone cannot keep the physical TX pin LOW. */
    GPIOB->BRR=GPIO_Pin_6;
    GPIOB->CRL=(GPIOB->CRL & ~(0xFul<<24)) | (0x2ul<<24);
    AFIO->MAPR &= ~AFIO_MAPR_TIM4_REMAP;
    TIM4->CR1=0u;
    TIM4->DIER=0u;
    TIM4->CCER=0u;
    TIM4->CR2=0u;
    TIM4->SMCR=0u;
    TIM4->CCMR1=TIM_CCMR1_CC1S_1 | TIM_CCMR1_CC2S_0;
    TIM4->CCMR2=0u;
    TIM4->PSC=71u; /* Board TIM4 input clock is 72MHz. */
    TIM4->ARR=65535u;
    TIM4->CNT=0u;
    TIM4->EGR=TIM_EGR_UG;
    TIM4->SR=0u;
    DMA1_Channel1->CCR=0u;
    DMA1_Channel4->CCR=0u;
    DMA1->IFCR=DMA_IFCR_CGIF1 | DMA_IFCR_CGIF4;
    DMA1_Channel1->CPAR=(uint32_t)&TIM4->CCR1;
    DMA1_Channel4->CPAR=(uint32_t)&TIM4->CCR2;
    DMA1_Channel1->CMAR=(uint32_t)g_ir_dma_probe.fall;
    DMA1_Channel4->CMAR=(uint32_t)g_ir_dma_probe.rise;
    DMA1_Channel1->CNDTR=IR_DMA_PROBE_CAPACITY;
    DMA1_Channel4->CNDTR=IR_DMA_PROBE_CAPACITY;
    /* Peripheral-to-memory, fixed peripheral, incrementing halfwords,
     * high priority, NORMAL mode, no DMA or TIM4 interrupts. */
    dma_config=DMA_CCR1_MINC | DMA_CCR1_PSIZE_0 | DMA_CCR1_MSIZE_0 |
               DMA_CCR1_PL_1;
    DMA1_Channel1->CCR=dma_config;
    DMA1_Channel4->CCR=dma_config;
    g_ir_dma_probe.fall_count=0u;
    g_ir_dma_probe.rise_count=0u;
    g_ir_dma_probe.end_timer=0u;
    g_ir_dma_probe.timer_status=0u;
    g_ir_dma_probe.dma_status=0u;
    g_ir_dma_probe.flags=0u;
    __DMB();
    DMA1_Channel1->CCR |= DMA_CCR1_EN;
    DMA1_Channel4->CCR |= DMA_CCR1_EN;
    TIM4->DIER=TIM_DIER_CC1DE | TIM_DIER_CC2DE;
    TIM4->CR1=TIM_CR1_CEN;
    g_ir_dma_probe.running=1u;
    __DMB();
    TIM4->CCER=TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E;
}

void IrDmaProbe_End(void) {
    uint32_t wait, pending, status, flags=0u;
    if (!g_ir_dma_probe.running) return;
    /* Close the physical capture gate FIRST. Leave DMA requests enabled
     * until any last CCR read has drained; simply disabling DMA first can
     * drop an edge already latched when the original collector said READY. */
    TIM4->CCER=0u;
    TIM4->CR1=0u;
    __DSB();
    g_ir_dma_probe.end_timer=(uint16_t)TIM4->CNT;
    for (wait=0u; wait<1024u; ++wait) {
        status=DMA1->ISR;
        pending=TIM4->SR & (TIM_SR_CC1IF | TIM_SR_CC2IF);
        if (!DMA1_Channel1->CNDTR || (status & DMA_ISR_TEIF1))
            pending &= ~TIM_SR_CC1IF;
        if (!DMA1_Channel4->CNDTR || (status & DMA_ISR_TEIF4))
            pending &= ~TIM_SR_CC2IF;
        if (!pending) break;
    }
    if (wait==1024u) flags |= IR_DMA_PROBE_DRAIN_TIMEOUT;
    TIM4->DIER=0u;
    __DSB();
    DMA1_Channel1->CCR &= ~DMA_CCR1_EN;
    DMA1_Channel4->CCR &= ~DMA_CCR1_EN;
    while ((DMA1_Channel1->CCR | DMA1_Channel4->CCR) & DMA_CCR1_EN) { }
    __DSB();
    __DMB();
    g_ir_dma_probe.fall_count=(uint16_t)(IR_DMA_PROBE_CAPACITY-
                                        DMA1_Channel1->CNDTR);
    g_ir_dma_probe.rise_count=(uint16_t)(IR_DMA_PROBE_CAPACITY-
                                        DMA1_Channel4->CNDTR);
    g_ir_dma_probe.timer_status=(uint16_t)TIM4->SR;
    status=DMA1->ISR & (DMA_ISR_GIF1 | DMA_ISR_TCIF1 | DMA_ISR_HTIF1 |
                       DMA_ISR_TEIF1 | DMA_ISR_GIF4 | DMA_ISR_TCIF4 |
                       DMA_ISR_HTIF4 | DMA_ISR_TEIF4);
    g_ir_dma_probe.dma_status=status;
    if (g_ir_dma_probe.fall_count==IR_DMA_PROBE_CAPACITY)
        flags |= IR_DMA_PROBE_FULL_FALL;
    if (g_ir_dma_probe.rise_count==IR_DMA_PROBE_CAPACITY)
        flags |= IR_DMA_PROBE_FULL_RISE;
    if (TIM4->SR & (TIM_SR_CC1OF | TIM_SR_CC2OF))
        flags |= IR_DMA_PROBE_OVERCAPTURE;
    if (status & (DMA_ISR_TEIF1 | DMA_ISR_TEIF4))
        flags |= IR_DMA_PROBE_DMA_ERROR;
    if (g_ir_dma_probe.fall_count<g_ir_dma_probe.rise_count ||
        g_ir_dma_probe.fall_count-g_ir_dma_probe.rise_count>1)
        flags |= IR_DMA_PROBE_INVALID;
    g_ir_dma_probe.flags=flags;
    __DMB();
    g_ir_dma_probe.running=0u; /* Publish frozen metadata/arrays last. */
}
#endif


uint32_t IrDmaProbe_Reconstruct(const volatile uint16_t *fall,
    uint16_t fall_count, const volatile uint16_t *rise, uint16_t rise_count,
    uint8_t first_rising, uint32_t input_flags, uint16_t *durations,
    uint16_t capacity, uint16_t *produced) {
    uint16_t f=0u, r=0u, previous=0u, current, total, i;
    uint8_t rising=first_rising;
    uint32_t flags=input_flags;
    if (!produced) return flags | IR_DMA_PROBE_INVALID;
    *produced=0u;
    if (fall_count>IR_DMA_PROBE_CAPACITY || rise_count>IR_DMA_PROBE_CAPACITY ||
        first_rising>1u || (fall_count && !fall) || (rise_count && !rise))
        return flags | IR_DMA_PROBE_INVALID;
    if ((!first_rising && (fall_count<rise_count || fall_count-rise_count>1)) ||
        (first_rising && (rise_count<fall_count || rise_count-fall_count>1)))
        return flags | IR_DMA_PROBE_INVALID;
    total=(uint16_t)(fall_count+rise_count);
    if (total>1u && (capacity<total-1u || !durations))
        return flags | IR_DMA_PROBE_OUTPUT_SHORT;
    if (fall_count==IR_DMA_PROBE_CAPACITY) flags |= IR_DMA_PROBE_FULL_FALL;
    if (rise_count==IR_DMA_PROBE_CAPACITY) flags |= IR_DMA_PROBE_FULL_RISE;
    for (i=0u; i<total; ++i) {
        current=rising ? rise[r++] : fall[f++];
        if (i!=0u) {
            uint16_t duration=(uint16_t)(current-previous);
            if (!duration) {
                *produced=0u;
                return flags | IR_DMA_PROBE_ZERO_INTERVAL;
            }
            durations[(*produced)++]=duration;
        }
        previous=current;
        rising=(uint8_t)!rising;
    }
    return flags;
}
