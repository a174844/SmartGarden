#ifndef SG_CLOCK_H
#define SG_CLOCK_H

#include <stdint.h>

/*
 * 系统时钟。
 *
 * 目标 SYSCLK = 168MHz（STM32F407 的上限）：
 *   AHB = 168MHz, APB1 = 42MHz, APB2 = 84MHz。
 * 这几个数字后面到处都在用（TIM3 的 PSC 按 84MHz 算、I2C1 的 CCR 按 42MHz 算），
 * 改这里就要连它们一起改。
 *
 * 必须在初始化任何外设之前调用：外设的时钟都从它出来。
 */
void clock_init_168mhz(void);

/* 1 = 用的是外部晶振，0 = 晶振没起振、退回了内部 RC。
   上电早期读一次就能判断板子上的晶振有没有虚焊。 */
uint32_t clock_hse_ok(void);

#endif /* SG_CLOCK_H */
