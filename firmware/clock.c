#include "clock.h"

#include "stm32f4xx.h"

/*
 * HSE 8MHz -> PLL -> 168MHz。
 *
 * 为什么不走 HAL_RCC_OscConfig / HAL_RCC_ClockConfig：
 * 这两个函数要先把 stm32f4xx_hal_rcc.c 整个拉进来（还有它的 _ex 依赖），
 * 而这里要做的其实只是按手册填几个寄存器。本工程其它外设（ADC/TIM3/I2C1）
 * 也都是直写寄存器的写法，保持一致。
 *
 * 频率换算（RM0090 §6.3.2）：
 *   VCO 输入 = HSE / PLL_M     必须落在 1-2MHz
 *   VCO 输出 = VCO 输入 x PLL_N  必须落在 100-432MHz
 *   SYSCLK   = VCO 输出 / PLL_P
 *   8MHz / 8 x 336 / 2 = 168MHz
 *   16MHz / 16 x 336 / 2 = 168MHz（晶振失效时走这条）
 */

#define PLL_M_HSE   8u    /* 8MHz  / 8  = 1MHz  */
#define PLL_M_HSI   16u   /* 16MHz / 16 = 1MHz  */
#define PLL_N       336u  /* 1MHz x 336  = 336MHz */
#define PLL_P       2u    /* 336MHz / 2  = 168MHz */
#define PLL_Q       7u    /* 48MHz 支路（USB/SDIO），本工程不用，按规格填 */

#define HSE_STARTUP_LOOPS   0x8000u

static uint32_t g_hse_ok;

uint32_t clock_hse_ok(void)
{
    return g_hse_ok;
}

void clock_init_168mhz(void)
{
    uint32_t loops = HSE_STARTUP_LOOPS;
    uint32_t pll_m = PLL_M_HSE;

    /*
     * Flash 等待周期必须**先**设好再提频。
     * 168MHz、VDD 3.3V 需要 5 个等待周期；先提频后改等待周期的话，
     * 中间那段取指跑在欠压时序上，可能读到错指令。
     * 顺手把指令/数据 cache 和预取打开，不然 Flash 会成为性能瓶颈
     * （168MHz 下单周期访问不可能，但有 cache 之后命中就不吃等待周期）。
     */
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN
               | FLASH_ACR_LATENCY_5WS;

    /*
     * 总线分频。上限：AHB 168MHz、APB1 42MHz、APB2 84MHz。
     * 注意 APB1 定时器时钟是 PCLK1 的两倍（=84MHz），TIM3 的 PSC 就是按它算的。
     */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    /* ---- 起 HSE ---- */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY) && --loops) { }

    g_hse_ok = (loops != 0u) ? 1u : 0u;
    if (!g_hse_ok) {
        /*
         * 晶振没起振（虚焊 / 没焊 / 频率不对）。退回内部 16MHz RC：
         * 精度差（RC 有百分之几的温漂），但对"土壤湿度 + 定时灌溉"这种
         * 秒级的应用完全够用，比整机停在 HSI 16MHz 上跑（PLL 都没起）强得多。
         * 现场排查时读 clock_hse_ok() 就能立刻区分是软件还是硬件问题。
         */
        pll_m = PLL_M_HSI;
    }

    /* ---- 配 PLL ---- */
    /* 改 PLLCFGR 前 PLL 必须处于关闭状态，否则写入被忽略 */
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) { }

    RCC->PLLCFGR = pll_m
                 | (PLL_N << RCC_PLLCFGR_PLLN_Pos)
                 | (((PLL_P / 2u) - 1u) << RCC_PLLCFGR_PLLP_Pos)
                 | (PLL_Q << RCC_PLLCFGR_PLLQ_Pos)
                 | (g_hse_ok ? RCC_PLLCFGR_PLLSRC_HSE : 0u);

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    /* ---- 切换 SYSCLK 到 PLL ---- */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }

    /*
     * 让 HAL / FreeRTOS 看到的 SystemCoreClock 与实际一致。
     * configCPU_CLOCK_HZ 是写死的 168MHz，SysTick 的重装值由它算出；
     * 这里算出来的值只用于 HAL 的延时换算与 HAL_RCC_GetHCLKFreq 之类的查询。
     */
    SystemCoreClockUpdate();
}
