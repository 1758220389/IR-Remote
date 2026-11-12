#include <assert.h>
#include <stdio.h>
#include "stm32f10x.h"
#include "board_keys.h"
GPIO_TypeDef mock_a, mock_b, mock_c;
static unsigned clocks, output_pins, input_pins;
void RCC_APB2PeriphClockCmd(uint32_t mask, int enable)
{ assert(enable == ENABLE); clocks |= mask; }
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    unsigned allowed;
    assert(clocks == (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC));
    if (init->GPIO_Mode == GPIO_Mode_Out_PP) {
        allowed = port == GPIOA ? GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_6 :
                  port == GPIOB ? GPIO_Pin_1 : GPIO_Pin_13;
        assert(init->GPIO_Speed == GPIO_Speed_2MHz);
        assert((port->BRR & init->GPIO_Pin) == init->GPIO_Pin);
        output_pins |= port == GPIOA ? 1 : port == GPIOB ? 2 : 4;
    } else {
        assert(init->GPIO_Mode == GPIO_Mode_IPU);
        allowed = port == GPIOA ? GPIO_Pin_2 | GPIO_Pin_5 :
                  port == GPIOB ? GPIO_Pin_0 | GPIO_Pin_11 : GPIO_Pin_15;
        input_pins |= port == GPIOA ? 1 : port == GPIOB ? 2 : 4;
    }
    assert(init->GPIO_Pin != 0 && (init->GPIO_Pin & ~allowed) == 0);
}
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin)
{ return (port->IDR & pin) ? 1 : 0; }
int main(void)
{
    BoardKeys_Init();
    assert(output_pins == 7 && input_pins == 7);
    assert(mock_a.BRR == (GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_6));
    assert(mock_b.BRR == GPIO_Pin_1 && mock_c.BRR == GPIO_Pin_13);
    puts("button low-before-mode, pull-ups, 2MHz GPIO: PASS");
    {
        unsigned mask;
        for (mask = 0; mask < 32; ++mask) {
            mock_a.IDR = mock_b.IDR = mock_c.IDR = 0xFFFFu;
            if (mask & 1) mock_c.IDR &= ~GPIO_Pin_15;
            if (mask & 2) mock_a.IDR &= ~GPIO_Pin_2;
            if (mask & 4) mock_a.IDR &= ~GPIO_Pin_5;
            if (mask & 8) mock_b.IDR &= ~GPIO_Pin_0;
            if (mask & 16) mock_b.IDR &= ~GPIO_Pin_11;
            assert(BoardKeys_ReadRaw() == mask);
        }
        puts("all 32 raw button combinations: PASS");
    }
    return 0;
}
