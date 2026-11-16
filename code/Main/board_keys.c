#include "board_keys.h"
#include "stm32f10x.h"

uint8_t BoardKeys_ReadRaw(void)
{
    uint8_t pressed = 0;
    if (!GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_15)) pressed |= 1u << 0;
    if (!GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2)) pressed |= 1u << 1;
    if (!GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5)) pressed |= 1u << 2;
    if (!GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_4)) pressed |= 1u << 3;
    if (!GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_12)) pressed |= 1u << 4;
    return pressed;
}

void BoardKeys_Init(void)
{
    GPIO_InitTypeDef init;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                          RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    /* PA15/PB4 need JTAG released; retain PA13/PA14 SWD debugging. */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    /* Preload low before enabling output drivers; 2MHz meets PC13 limit. */
    GPIOA->BRR = GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_10 | GPIO_Pin_15;
    GPIOC->BRR = GPIO_Pin_13;
    init.GPIO_Speed = GPIO_Speed_2MHz;
    init.GPIO_Mode = GPIO_Mode_Out_PP;
    init.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_10 | GPIO_Pin_15;
    GPIO_Init(GPIOA, &init);
    init.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &init);
    init.GPIO_Mode = GPIO_Mode_IPU;
    init.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_5 | GPIO_Pin_12;
    GPIO_Init(GPIOA, &init);
    init.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOB, &init);
    init.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init(GPIOC, &init);
}
