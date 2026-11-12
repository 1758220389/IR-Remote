#ifndef MOCK_STM32F10X_H
#define MOCK_STM32F10X_H
#include <stdint.h>
typedef struct { volatile uint32_t BRR, IDR, ODR; } GPIO_TypeDef;
extern GPIO_TypeDef mock_a, mock_b, mock_c;
#define GPIOA (&mock_a)
#define GPIOB (&mock_b)
#define GPIOC (&mock_c)
#define GPIO_Pin_0 (1u << 0)
#define GPIO_Pin_1 (1u << 1)
#define GPIO_Pin_2 (1u << 2)
#define GPIO_Pin_3 (1u << 3)
#define GPIO_Pin_5 (1u << 5)
#define GPIO_Pin_6 (1u << 6)
#define GPIO_Pin_8 (1u << 8)
#define GPIO_Pin_9 (1u << 9)
#define GPIO_Pin_11 (1u << 11)
#define GPIO_Pin_13 (1u << 13)
#define GPIO_Pin_15 (1u << 15)
#define RCC_APB2Periph_GPIOA 4u
#define RCC_APB2Periph_GPIOB 8u
#define RCC_APB2Periph_GPIOC 16u
#define ENABLE 1
#define GPIO_Mode_Out_PP 1
#define GPIO_Mode_Out_OD 2
#define GPIO_Mode_IPU 3
#define GPIO_Speed_2MHz 2
#define GPIO_Speed_50MHz 50
typedef enum { Bit_RESET, Bit_SET } BitAction;
typedef struct { uint16_t GPIO_Pin; unsigned GPIO_Speed; unsigned GPIO_Mode; } GPIO_InitTypeDef;
void RCC_APB2PeriphClockCmd(uint32_t mask, int enable);
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void GPIO_WriteBit(GPIO_TypeDef *port, uint16_t pin, BitAction value);
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin);
#endif
