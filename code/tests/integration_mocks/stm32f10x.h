#ifndef STM32_TEST_H
#define STM32_TEST_H
#include <stdint.h>
extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
uint32_t SysTick_Config(uint32_t ticks);
#endif
