/**
  ******************************************************************************
  * @file    stm32f4xx_hal_conf.h
  * @brief   HAL 裁剪配置（STM32F407）。
  *
  *          按 ST 模板 stm32f4xx_hal_conf_template.h 改的，区别只有两处：
  *            1) 只留本工程真正用到的模块；
  *            2) HSE 按 8MHz 晶振写（模板默认 25MHz，那是 F4 探索板的值）。
  ******************************************************************************
  */

#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */
/*
 * 只开用得到的模块。关掉的模块对应的源文件也不参与编译 ——
 * HAL 的各个 .c 之间互相引用，开一个模块往往要把它的依赖链一起拉进来，
 * 少开一个就少一串。
 *
 * 对应的源文件清单在 tools/build_arm.py 的 HAL_SRC 里，两边要一起改。
 */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED   /* HAL_Init 里的 NVIC 优先级分组        */
#define HAL_RCC_MODULE_ENABLED      /* 时钟树维护、SystemCoreClock 更新     */
#define HAL_GPIO_MODULE_ENABLED     /* BLE 的 PA2/PA3 复用配置              */
#define HAL_DMA_MODULE_ENABLED      /* 没直接用，但 HAL_UART 的 DMA 分支引用它 */
#define HAL_FLASH_MODULE_ENABLED    /* 参数区擦写                           */
#define HAL_IWDG_MODULE_ENABLED     /* 独立看门狗                           */
#define HAL_PWR_MODULE_ENABLED      /* hal_rcc 的若干分支引用                */
#define HAL_UART_MODULE_ENABLED     /* BLE 透传串口 USART2                  */

/* 下面这些本工程是直接写寄存器的，不需要 HAL 驱动：
   ADC1（board_stm32f4.c 直写 ADC1->xxx）
   TIM3 PWM（直写 TIM3->xxx）
   I2C1 OLED（直写 I2C1->xxx，见 board_i2c_write）
   这么做省掉了 CubeMX 那一套句柄初始化，代价是时序要自己核对，
   所以裸寄存器那几处都写了 CCR/TRISE/采样时间的推导过程。 */

/* ########################## HSE/HSI Values adaptation ##################### */

/*
 * 外部晶振 8MHz。板子上换晶振的话改这一处，同时要改 firmware/clock.c
 * 里的 PLL_M —— 两处必须自洽，否则 PLL 输出频率会被拉偏。
 *
 * 晶振不起振时 clock.c 会退回内部 HSI(16MHz)，同样收敛到 168MHz，
 * 所以这个值不对不会让系统停摆，只是绝对频率不准。
 */
#if !defined  (HSE_VALUE)
  #define HSE_VALUE              8000000U
#endif

#if !defined  (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif

#if !defined  (HSI_VALUE)
  #define HSI_VALUE              16000000U
#endif

#if !defined  (LSI_VALUE)
  #define LSI_VALUE              32000U
#endif

#if !defined  (LSE_VALUE)
  #define LSE_VALUE              32768U
#endif

#if !defined  (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    5000U
#endif

#if !defined  (EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE   12288000U
#endif

/* ########################### System Configuration ######################### */

#define  VDD_VALUE                    3300U
#define  TICK_INT_PRIORITY            0x0FU
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE              1U
#define  INSTRUCTION_CACHE_ENABLE     1U
#define  DATA_CACHE_ENABLE            1U

#define  USE_HAL_UART_REGISTER_CALLBACKS        0U

/* ########################## Assert Selection ############################## */
/* 打开后 HAL 会检查每个参数，代价是代码变大、每个调用点多一次判断。
   本工程在做 bring-up，暂时关掉；定位问题时打开很有用。 */
/* #define USE_FULL_ASSERT    1U */

/* Includes ------------------------------------------------------------------*/

#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f4xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f4xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f4xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f4xx_hal_cortex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f4xx_hal_flash.h"
#endif

#ifdef HAL_IWDG_MODULE_ENABLED
  #include "stm32f4xx_hal_iwdg.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f4xx_hal_pwr.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32f4xx_hal_uart.h"
#endif

/* Exported macro ------------------------------------------------------------*/
#ifdef  USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_HAL_CONF_H */
