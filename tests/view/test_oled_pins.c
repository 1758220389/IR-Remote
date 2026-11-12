#include <assert.h>
#include <stdio.h>
#include "stm32f10x.h"
GPIO_TypeDef mock_a, mock_b, mock_c;
static unsigned last_pin, last_value, clocks, configured;
void OLED_W_SCL(uint8_t value);
void OLED_W_SDA(uint8_t value);
void OLED_GPIO_Init(void);
void RCC_APB2PeriphClockCmd(uint32_t mask, int enable)
{ assert(enable == ENABLE); clocks |= mask; }
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    assert(clocks == RCC_APB2Periph_GPIOB && port == GPIOB);
    assert(init->GPIO_Mode == GPIO_Mode_Out_OD);
    configured |= init->GPIO_Pin;
}
void GPIO_WriteBit(GPIO_TypeDef *port, uint16_t pin, BitAction value)
{ assert(port == GPIOB); last_pin = pin; last_value = value; }
int main(void)
{
    OLED_W_SCL(0);
    assert(last_pin == GPIO_Pin_9 && last_value == 0);
    OLED_W_SCL(1);
    assert(last_pin == GPIO_Pin_9 && last_value == 1);
    OLED_W_SDA(0);
    assert(last_pin == GPIO_Pin_8 && last_value == 0);
    OLED_W_SDA(1);
    assert(last_pin == GPIO_Pin_8 && last_value == 1);
    OLED_GPIO_Init();
    assert(configured == (GPIO_Pin_8 | GPIO_Pin_9));
    puts("OLED SCL=PB9 SDA=PB8 and open-drain initialization: PASS");
    return 0;
}
