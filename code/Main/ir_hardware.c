#include "ir_hardware.h"
#include "ir_capture.h"
#include "bosch144.h"
#include "ir_dma_probe.h"
#ifdef IR_HARDWARE_TEST
extern void IrPlatform_Init(void);
extern uint32_t IrPlatform_Now(void);
extern int IrPlatform_High(void);
extern void IrPlatform_RxMask(int enabled);
extern void IrPlatform_Gate(int mark);
extern void IrPlatform_Arm(uint16_t us);
extern void IrPlatform_Stop(void);
extern void IrPlatform_Carrier(uint16_t khz);
extern int IrPlatform_RxPending(void);
extern int IrPlatform_TxPending(void);
extern uint16_t IrPlatform_FallCount(void);
extern uint16_t IrPlatform_RiseCount(void);
extern uint16_t IrPlatform_RxTimer(void);
#else
#include "stm32f10x.h"
/* TIM2 is SIXTEEN bit on STM32F103, extended by its update interrupt. */
static volatile uint32_t timer2_upper;
static uint32_t IrPlatform_Now(void) {
    uint32_t mask=__get_PRIMASK(), upper, low;
    __disable_irq();
    upper=timer2_upper;
    low=TIM2->CNT;
    if (TIM2->SR & TIM_SR_UIF) { upper+=65536u; low=TIM2->CNT; }
    __set_PRIMASK(mask);
    return upper+low;
}
void TIM2_IRQHandler(void) {
    uint32_t mask=__get_PRIMASK();
    /* TIM3 can preempt TIM2 and read a timestamp at end of TX. Keep the
     * high-word/UIF pair coherent for that reader, just a few instructions. */
    __disable_irq();
    if (TIM2->SR & TIM_SR_UIF) {
        timer2_upper+=65536u;
        TIM2->SR=(uint16_t)~TIM_SR_UIF;
    }
    __set_PRIMASK(mask);
}
static int IrPlatform_High(void) { return (GPIOB->IDR & GPIO_Pin_7)!=0u; }
static uint16_t IrPlatform_FallCount(void) {
    return (uint16_t)(IR_DMA_PROBE_CAPACITY-DMA1_Channel1->CNDTR);
}
static uint16_t IrPlatform_RiseCount(void) {
    return (uint16_t)(IR_DMA_PROBE_CAPACITY-DMA1_Channel4->CNDTR);
}
static uint16_t IrPlatform_RxTimer(void) { return (uint16_t)TIM4->CNT; }
static void IrPlatform_RxMask(int enabled) {
    (void)enabled;
    EXTI->IMR &= ~EXTI_Line7;
}
static int IrPlatform_RxPending(void) {
    int pending=(EXTI->PR & EXTI_Line7)!=0u;
    EXTI->PR=EXTI_Line7;
    return pending;
}
static int IrPlatform_TxPending(void) {
    int pending=(TIM3->SR & TIM_SR_UIF)!=0u;
    TIM3->SR=(uint16_t)~TIM_SR_UIF;
    return pending;
}
static void IrPlatform_Gate(int mark) {
    /* Keep CC1 enabled; forced-inactive drives PB6 physically LOW. Merely
     * disabling CC1 can leave the output high on this MCU. */
    if (mark) {
        TIM4->CNT=0u;
        TIM_SelectOCxM(TIM4, TIM_Channel_1, TIM_OCMode_PWM1);
    } else TIM_SelectOCxM(TIM4, TIM_Channel_1, TIM_ForcedAction_InActive);
    TIM_CCxCmd(TIM4, TIM_Channel_1, TIM_CCx_Enable);
}
static void IrPlatform_Arm(uint16_t us) {
    TIM3->CR1=TIM_CR1_OPM;
    TIM3->DIER=0u;
    TIM3->ARR=(uint16_t)(us-1u);
    TIM3->CNT=0u;
    TIM3->EGR=TIM_EGR_UG;
    TIM3->SR=0u;
    TIM3->DIER=TIM_DIER_UIE;
    TIM3->CR1=TIM_CR1_OPM | TIM_CR1_CEN;
}
static void IrPlatform_Stop(void) {
    TIM3->CR1=TIM_CR1_OPM;
    TIM3->DIER=0u;
    TIM3->SR=0u;
}
static void IrPlatform_Carrier(uint16_t khz) {
    GPIO_InitTypeDef gpio;
    TIM_OCInitTypeDef oc;
    uint32_t period=(72000u+khz/2u)/khz;
    /* DMA diagnostics use CH1 as TI2 input at 1MHz. Restore the COMPLETE
     * output configuration before reconnecting PB6 to the timer. */
    TIM4->CR1=0u; TIM4->DIER=0u; TIM4->CCER=0u;
    TIM4->CCMR1=0u; TIM4->SMCR=0u;
    TIM4->PSC=0u;
    TIM4->ARR=(uint16_t)(period-1u);
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode=TIM_ForcedAction_InActive;
    oc.TIM_OutputState=TIM_OutputState_Enable;
    oc.TIM_OCPolarity=TIM_OCPolarity_High;
    oc.TIM_Pulse=(uint16_t)(period/3u);
    TIM_OC1Init(TIM4,&oc);
    TIM4->CNT=0u; TIM4->EGR=TIM_EGR_UG; TIM4->SR=0u;
    TIM4->CR1=TIM_CR1_CEN;
    gpio.GPIO_Pin=GPIO_Pin_6; gpio.GPIO_Mode=GPIO_Mode_AF_PP;
    gpio.GPIO_Speed=GPIO_Speed_2MHz;
    GPIO_Init(GPIOB,&gpio);
}
static void IrPlatform_Init(void) {
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef time;
    TIM_OCInitTypeDef oc;
    EXTI_InitTypeDef exti;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 |
                           RCC_APB1Periph_TIM4, ENABLE);
    GPIO_ResetBits(GPIOB, GPIO_Pin_6);
    gpio.GPIO_Pin=GPIO_Pin_6; gpio.GPIO_Mode=GPIO_Mode_Out_PP;
    gpio.GPIO_Speed=GPIO_Speed_2MHz; GPIO_Init(GPIOB, &gpio);
    TIM_TimeBaseStructInit(&time);
    time.TIM_Prescaler=0u; time.TIM_Period=1894u;
    TIM_TimeBaseInit(TIM4, &time);
    TIM_OCStructInit(&oc);
    oc.TIM_OCMode=TIM_ForcedAction_InActive;
    oc.TIM_OutputState=TIM_OutputState_Enable;
    oc.TIM_Pulse=631u; TIM_OC1Init(TIM4, &oc);
    TIM_Cmd(TIM4, ENABLE);
    gpio.GPIO_Mode=GPIO_Mode_AF_PP; GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Pin=GPIO_Pin_7; gpio.GPIO_Mode=GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource7);
    exti.EXTI_Line=EXTI_Line7; exti.EXTI_Mode=EXTI_Mode_Interrupt;
    exti.EXTI_Trigger=EXTI_Trigger_Rising_Falling; exti.EXTI_LineCmd=DISABLE;
    EXTI_Init(&exti); EXTI_ClearITPendingBit(EXTI_Line7);
    /* EXTI_Init(DISABLE) does not program trigger selection in all SPL
     * revisions; set both explicitly while keeping delivery masked. */
    EXTI->RTSR |= EXTI_Line7; EXTI->FTSR |= EXTI_Line7;
    time.TIM_Prescaler=71u; time.TIM_Period=65535u;
    TIM_TimeBaseInit(TIM2, &time); TIM_TimeBaseInit(TIM3, &time);
    timer2_upper=0u;
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_SelectOnePulseMode(TIM3, TIM_OPMode_Single);
    /* CMSIS encoded priorities avoid SPL NVIC_Init's dependence on a
     * previously configured priority group. Do not change global grouping.
     * EXTI delivery remains disabled; DMA owns all receiver edges. */
    NVIC_SetPriority(TIM2_IRQn, 1u);
    NVIC_SetPriority(EXTI9_5_IRQn, 1u);
    NVIC_SetPriority(TIM3_IRQn, 0u);
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_DisableIRQ(EXTI9_5_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
    TIM_Cmd(TIM2, ENABLE);
}
#endif

static UiSignal transmit;
static volatile uint8_t transmitting;
static uint16_t tx_index, tx_count;
static uint8_t receive_requested, receive_active, ready;
static volatile uint8_t settling;
static uint16_t seen_fall, seen_rise;
static uint8_t acquisition_started;
static volatile uint32_t settle_at;
static uint32_t acquisition_at, last_edge_clock, receive_error;
uint32_t IrHardware_LastReceiveError(void) { return receive_error; }

static uint16_t captured_duration(void *context, uint16_t index) {
    uint16_t pair=(uint16_t)(index/2u);
    (void)context;
    if (!(index&1u))
        return (uint16_t)(g_ir_dma_probe.rise[pair]-g_ir_dma_probe.fall[pair]);
    return (uint16_t)(g_ir_dma_probe.fall[pair+1u]-g_ir_dma_probe.rise[pair]);
}
void IrHardware_Init(void) {
    IrDmaProbe_End();
    IrPlatform_Init();
    IrPlatform_RxMask(0);
    IrPlatform_Gate(0);
    IrPlatform_Stop();
    transmitting=0u; receive_error=0u;
    receive_requested=receive_active=settling=ready=0u;
}
void IrHardware_SetReceiveEnabled(uint8_t enabled) {
    enabled=(uint8_t)!!enabled;
    if (enabled==receive_requested) return;
    IrDmaProbe_End();
    receive_active=ready=0u; receive_error=0u;
    receive_requested=enabled;
    settle_at=IrPlatform_Now();
    settling=enabled;
}
void IrHardware_Poll(uint32_t now_ms) {
    uint32_t now;
    uint16_t falls, rises, timer, last;
    int high;
    (void)now_ms;
    if (!receive_requested || transmitting || ready) return;
    now=IrPlatform_Now(); high=IrPlatform_High();
    if (settling) {
        if (!high) settle_at=now;
        else if ((uint32_t)(now-settle_at)>=IR_CAPTURE_GAP_US) {
            IrDmaProbe_Begin();
            seen_fall=seen_rise=0u; acquisition_started=0u;
            settling=0u; receive_active=1u;
        }
        return;
    }
    if (!receive_active) return;
    falls=IrPlatform_FallCount(); rises=IrPlatform_RiseCount();
    timer=IrPlatform_RxTimer();
    if (falls!=seen_fall || rises!=seen_rise) {
        if (!acquisition_started) { acquisition_at=now; acquisition_started=1u; }
        seen_fall=falls; seen_rise=rises; last_edge_clock=now;
    }
    if (!acquisition_started) return;
    if ((uint32_t)(now-acquisition_at)>=1000000u) {
        IrDmaProbe_End(); receive_active=0u; ready=1u;
        receive_error=IR_RX_ERROR_TIMEOUT;
        return;
    }
    if (falls>=IR_DMA_PROBE_CAPACITY || rises>=IR_DMA_PROBE_CAPACITY) {
        IrDmaProbe_End(); receive_active=0u; ready=1u;
        receive_error=IR_RX_ERROR_CAPACITY;
        return;
    }
    last=rises ? g_ir_dma_probe.rise[rises-1u] : g_ir_dma_probe.fall[falls-1u];
    if (high && (uint32_t)(now-last_edge_clock)>=IR_CAPTURE_GAP_US &&
        (uint16_t)(timer-last)>=IR_CAPTURE_GAP_US) {
        IrDmaProbe_End(); receive_active=0u; ready=1u;
    }
}
int IrHardware_TryReceive(UiSignal *dest) {
    uint16_t i, count;
    if (!ready || !receive_requested || transmitting || !dest) return 0;
    count=(uint16_t)(g_ir_dma_probe.fall_count+g_ir_dma_probe.rise_count-1u);
    if (receive_error) return -1;
    if (g_ir_dma_probe.flags) receive_error=IR_RX_ERROR_DMA | g_ir_dma_probe.flags;
    else if (!g_ir_dma_probe.fall_count ||
        g_ir_dma_probe.fall_count!=g_ir_dma_probe.rise_count || count<7u)
        receive_error=IR_RX_ERROR_COUNTS;
    if (receive_error) return -1;
    for (i=0u;i<count;++i) if (captured_duration(0,i)<100u) {
        receive_error=IR_RX_ERROR_INTERVAL; return -1;
    }
    /* A Bosch-like header must never fall back to a learnable prefix. */
    if (count==299u ||
        (captured_duration(0,0u)>=3274u && captured_duration(0,0u)<=5458u &&
         captured_duration(0,1u)>=3311u && captured_duration(0,1u)<=5519u)) {
        if (!Bosch144_Decode(captured_duration,0,count,dest)) {
            receive_error=IR_RX_ERROR_PROTOCOL; return -1;
        }
    } else {
        if (count>255u) { receive_error=IR_RX_ERROR_CAPACITY; return -1; }
        dest->length=count; dest->carrier_khz=38u; dest->format=UI_SIGNAL_RAW;
        for (i=0u;i<count;++i) dest->timings[i]=captured_duration(0,i);
    }
    ready=0u; settling=1u; settle_at=IrPlatform_Now();
    return 1;
}
void EXTI9_5_IRQHandler(void) { (void)IrPlatform_RxPending(); }
int IrHardware_Busy(void) { return transmitting!=0u; }
int IrHardware_Send(const UiSignal *signal) {
    uint16_t i;
    if (transmitting || !signal || signal->carrier_khz<30u ||
        signal->carrier_khz>60u) return 0;
    if (signal->format==UI_SIGNAL_BOSCH144) {
        if (!Bosch144_ValidateSignal(signal)) return 0;
        tx_count=Bosch144_DurationCount(signal);
    } else if (signal->format==UI_SIGNAL_RAW) {
        if (!signal->length || signal->length>255u || !(signal->length&1u)) return 0;
        for (i=0u;i<signal->length;++i) if (!signal->timings[i]) return 0;
        tx_count=signal->length;
    } else return 0;
    IrPlatform_RxMask(0);
    IrDmaProbe_End();
    receive_active=0u;
    settling=1u;
    ready=0u; receive_error=0u;
    transmit=*signal;
    tx_index=0u;
    transmitting=1u;
    IrPlatform_Carrier(transmit.carrier_khz);
    IrPlatform_Gate(1);
    IrPlatform_Arm(transmit.format==UI_SIGNAL_BOSCH144 ?
                   Bosch144_Duration(&transmit,0u) : transmit.timings[0]);
    return 1;
}
void TIM3_IRQHandler(void) {
    if (!IrPlatform_TxPending() || !transmitting) return;
    ++tx_index;
    if (tx_index>=tx_count) {
        IrPlatform_Gate(0);
        IrPlatform_Stop();
        settle_at=IrPlatform_Now();
        settling=1u;
        transmitting=0u;
        return;
    }
    IrPlatform_Gate(!(tx_index&1u));
    IrPlatform_Arm(transmit.format==UI_SIGNAL_BOSCH144 ?
                   Bosch144_Duration(&transmit,tx_index) : transmit.timings[tx_index]);
}
