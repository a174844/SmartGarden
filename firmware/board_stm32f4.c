#include "board.h"
#include "clock.h"
#include "dht22.h"
#include "uart_ble.h"

#include "stm32f4xx_hal.h"

/*
 * STM32F407 目标板实现。引脚分配：
 *
 *   PA0     土壤湿度传感器（模拟输出）   -> ADC1_IN0
 *   PA1     光敏电阻分压                 -> ADC1_IN1
 *   PA2/PA3 BLE 透传模块 UART（TX/RX）   -> USART2（AF7）
 *   PA6     TIM3_CH1 水泵 PWM            -> AF2
 *   PA7     TIM3_CH2 补光 PWM            -> AF2
 *   PA8     DHT22 数据线（开漏 + 外部上拉）
 *   PB6/PB7 OLED 状态屏 SCL/SDA          -> I2C1（AF4，开漏）
 *   PD0     心跳指示灯（推挽，低电平点亮）
 *
 * 时钟（firmware/clock.c）：SYSCLK 168MHz，AHB 168MHz，APB1 42MHz，APB2 84MHz。
 *   - TIM3 挂在 APB1 上，APB1 分频系数 != 1，所以定时器时钟 = 2 x PCLK1 = 84MHz；
 *     PSC=83 -> 1MHz，ARR=999 -> 1kHz，分辨率千分之一。
 *   - I2C1 挂在 APB1 上，CCR 按 42MHz 算。
 *   - ADC 时钟上限 36MHz，APB2 是 84MHz，所以 ADC 预分频取 /4 = 21MHz。
 *
 * 参数区：主 Flash 扇区 11（0x080E0000 起，128KB）。
 */

#define PWM_ARR          999u
#define FLASH_PARAM_ADDR 0x080E0000u
#define FLASH_SECTOR     FLASH_SECTOR_11

/*
 * F407VG 的扇区大小是不均匀的：0-3 扇区 16KB，4 是 64KB，5-11 是 128KB。
 * 参数区落在最后一个扇区（11），所以这里是 128KB。
 * 这个宏必须按实际扇区给：这里曾经直接用了 FLASH_SECTOR_SIZE 却从没定义过，
 * 编译器第一次看到就报 undeclared identifier。
 */
#define FLASH_SECTOR_SIZE 0x20000u

#define LED_PIN          (1u << 0)      /* PD0 */
#define LED_PORT         GPIOD

/* 定义在文件后半部分，board_init 里要用 */
static void i2c1_init(void);

/*
 * 微秒级延时，DHT22 单总线时序要用。
 * 用 DWT 周期计数器而不是空循环：空循环的耗时随编译器优化等级变化，
 * 换一次 -O 等级时序就崩了。
 *
 * 注意它不依赖任何中断，因此在**关中断的临界区里也能用** ——
 * DHT22 的时序正是这种情况，而同为延时函数的 HAL_Delay() 在那里会死等
 * （HAL 的计数靠 SysTick 中断推进）。
 */
static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < ticks) { }
}

int board_init(void)
{
    /*
     * 顺序有讲究：
     *   1) 先把系统时钟拉到 168MHz —— 下面所有外设的时序参数都是按它算的；
     *   2) 再初始化 HAL —— HAL_InitTick 被 freertos_hooks.c 覆盖成了空实现，
     *      SysTick 留给 FreeRTOS 自己配（见 FreeRTOSConfig.h 末尾的说明）；
     *   3) 最后开各外设的时钟并配引脚。
     */
    clock_init_168mhz();
    HAL_Init();

    /* DWT 周期计数器，delay_us 的时基 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    /*
     * 外设时钟。这一段漏掉任何一个，表现都是"寄存器写进去了、读回来也对，
     * 但外设毫无反应"—— 排查起来极其费时，所以集中列在这里。
     */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN | RCC_APB1ENR_I2C1EN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    /* ---- 引脚 ---- */

    /* PA0/PA1：模拟输入（ADC1_IN0 / IN1）。模拟模式下 MODER = 11 */
    GPIOA->MODER |= (3u << 0) | (3u << 2);

    /* PA6/PA7：复用推挽，AF2 -> TIM3_CH1/CH2 */
    GPIOA->MODER  = (GPIOA->MODER & ~((3u << 12) | (3u << 14))) | (2u << 12) | (2u << 14);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((0xFu << 24) | (0xFu << 28)))
                  | (2u << 24) | (2u << 28);

    /* PA8：开漏输出，空闲靠外部上拉拉高 */
    GPIOA->MODER   = (GPIOA->MODER & ~(3u << 16)) | (1u << 16);
    GPIOA->OTYPER |= (1u << 8);

    /* PD0：心跳灯，推挽输出，先点亮（低电平有效）便于确认程序跑起来了 */
    LED_PORT->MODER = (LED_PORT->MODER & ~(3u << 0)) | (1u << 0);
    board_led_set(1);

    /* ---- ADC1 ---- */
    /*
     * ADC 时钟 = PCLK2 / 预分频。PCLK2 = 84MHz，而 F407 的 ADC 上限是 36MHz，
     * 所以必须分频：/4 -> 21MHz。用默认的 /2 是 42MHz，超规格，
     * 读数会随温度漂、并且手册明确说不保证精度。
     */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
    ADC1->CR1  = 0;
    ADC1->CR2  = 0;
    ADC1->SQR1 = 0;
    /* 采样时间取最长（480 周期）：土壤探头与光敏分压的源阻抗都偏高，
       采样保持电容来不及充满，采太快读数会系统性偏低 */
    ADC1->SMPR2 = (7u << 0) | (7u << 3);
    ADC1->CR2  |= ADC_CR2_ADON;         /* 上电，转换由 board_adc_read 里的 SWSTART 触发 */

    /* ---- TIM3 PWM，CH1/CH2 ---- */
    TIM3->PSC   = 83;                   /* 84MHz / 84 = 1MHz */
    TIM3->ARR   = PWM_ARR;
    TIM3->CCMR1 = (6u << 4) | (1u << 3) | (6u << 12) | (1u << 11);   /* PWM 模式 1 + 预装载 */
    TIM3->CCER  = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM3->CR1  |= TIM_CR1_ARPE | TIM_CR1_CEN;

    board_pwm_set(SG_PWM_PUMP,  0);
    board_pwm_set(SG_PWM_LIGHT, 0);

    i2c1_init();
    ble_uart_init();

    board_watchdog_init(4000);

    return 0;
}

/* ---------------- I2C1：OLED 状态屏 ---------------- */

/*
 * PB6 = I2C1_SCL，PB7 = I2C1_SDA，复用 AF4，开漏 + 外部上拉（模块上自带 4.7k）。
 *
 * 这里直接用寄存器而不走 HAL：一是和上面 ADC/TIM3 的写法一致，
 * 二是省掉一份 CubeMX 生成的 hi2c1 初始化（本工程不是 CubeMX 工程）。
 *
 * 时钟按 APB1 = 42MHz 算：
 *   CCR = 42MHz / (3 x 400kHz) = 35，且必须置 F/S 位选快速模式、
 *         DUTY=0 表示 Tlow:Thigh = 2:1（周期 = 3 x CCR x Tpclk）。
 *         漏掉 F/S 位的话外设按标准模式解释 CCR，
 *         SCL 会跑到 42MHz/(2x35) = 600kHz —— 超出从机能力，屏会花。
 *   TRISE = 300ns / (1/42MHz) + 1 ≈ 14（快速模式允许的最大上升沿时间）
 */
#define I2C1_PCLK1_MHZ   42u
#define I2C1_CCR_400K    (I2C_CCR_FS | (I2C1_PCLK1_MHZ * 1000u / (3u * 400u)))  /* = 0x8023 */
#define I2C1_TRISE_VAL   14u

/*
 * 每一步等待都带超时计数。理由：如果面板没插好（SDA 被从机拉死），
 * 无超时的死等会把调用它的任务永久卡住 —— 监控任务会判定任务卡死，
 * 最后靠看门狗复位整机。刷新一块屏失败最多是花屏，不该拖垮系统。
 */
#define I2C1_TIMEOUT    200000u

static void i2c1_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* PB6/PB7：复用模式、开漏、内部上拉（外部 4.7k 为主，内部上拉只是兜底） */
    GPIOB->MODER   = (GPIOB->MODER  & ~((3u << 12) | (3u << 14)))
                   | (2u << 12) | (2u << 14);
    GPIOB->OTYPER |= (1u << 6) | (1u << 7);
    GPIOB->PUPDR   = (GPIOB->PUPDR  & ~((3u << 12) | (3u << 14)))
                   | (1u << 12) | (1u << 14);
    GPIOB->AFR[0]  = (GPIOB->AFR[0] & ~((0xFu << 24) | (0xFu << 28)))
                   | (4u << 24) | (4u << 28);

    I2C1->CR1   = I2C_CR1_SWRST;    /* 软复位，清掉上电时可能残留的状态 */
    delay_us(10);                   /* 让复位状态稳定一拍再放开 */
    I2C1->CR1   = 0;
    I2C1->CR2   = I2C1_PCLK1_MHZ;   /* 告诉外设 APB1 频率，用于算时序 */
    I2C1->CCR   = I2C1_CCR_400K;
    I2C1->TRISE = I2C1_TRISE_VAL;
    I2C1->CR1  |= I2C_CR1_PE;       /* 使能 */
}

/* 等某个 SR1 标志置位，超时返回 -1 */
static int i2c1_wait(uint32_t mask)
{
    uint32_t t = 0;

    while (!(I2C1->SR1 & mask)) {
        if (++t > I2C1_TIMEOUT) return -1;
    }
    return 0;
}

/* 发 START + 从机地址（写方向）。无应答时补一个 STOP 释放总线 */
static int i2c1_start(uint8_t addr)
{
    uint32_t t = 0;

    I2C1->CR1 |= I2C_CR1_START;
    if (i2c1_wait(I2C_SR1_SB) != 0) return -1;

    I2C1->DR = (uint16_t)((uint16_t)addr << 1);

    /* 这里要同时等 ADDR 和 AF：从机不应答时置的是 AF 而不是 ADDR，
       只等 ADDR 会一直等到超时，白白拖慢一次刷新 */
    while (!(I2C1->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF))) {
        if (++t > I2C1_TIMEOUT) {
            I2C1->CR1 |= I2C_CR1_STOP;
            return -1;
        }
    }
    if (I2C1->SR1 & I2C_SR1_AF) {
        I2C1->SR1 &= ~I2C_SR1_AF;       /* 写 0 清 AF */
        I2C1->CR1 |= I2C_CR1_STOP;
        return -1;
    }

    (void)I2C1->SR1;                    /* 先读 SR1 */
    (void)I2C1->SR2;                    /* 再读 SR2，才真正清掉 ADDR */
    return 0;
}

int board_i2c_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (!buf || len == 0) return -1;

    if (i2c1_start(addr) != 0) return -1;

    for (i = 0; i < len; i++) {
        if (i2c1_wait(I2C_SR1_TXE) != 0) {
            I2C1->CR1 |= I2C_CR1_STOP;
            return -1;
        }
        I2C1->DR = buf[i];
    }

    /* 最后一个字节要等 BTF（字节传输完成）再发 STOP，
       否则 DR 里的数据会在 STOP 之前被丢掉，表现为屏幕最后一段花掉 */
    if (i2c1_wait(I2C_SR1_BTF) != 0) {
        I2C1->CR1 |= I2C_CR1_STOP;
        return -1;
    }

    I2C1->CR1 |= I2C_CR1_STOP;
    return 0;
}

/* ---------------- 心跳指示灯 ---------------- */

void board_led_set(int on)
{
    if (on) LED_PORT->BSRR = LED_PIN;                  /* 置位 -> 输出高 */
    else    LED_PORT->BSRR = (LED_PIN << 16);          /* 复位 -> 输出低 */
}

/* ---------------- ADC ---------------- */

uint16_t board_adc_read(sg_adc_ch_t ch)
{
    ADC1->SQR3 = (uint32_t)ch;          /* 本次转换的通道 */

    /*
     * 软件启动一次规则转换。
     *
     * 这里必须写 SWSTART，而不是像 F1/F2 那样"再写一次 ADON"：
     * F4 的 ADC_CR2.ADON 只负责上电/掉电，手册里明确它不是启动位。
     * 写 ADON 的代码在 F4 上能编译、能跑、EOC 永远不来 —— 表现为第一次
     * 读 ADC 就把任务挂死。这也是本文件从"只过语法检查"到"真跑"之间
     * 最容易漏掉的一处。
     *
     * 转换超时这里不做限制：ADC 没配好（时钟没开、ADON 没置）时 EOC 不会来，
     * 死等会让采集任务卡住，监控任务随即停止喂狗，独立看门狗 4 秒内复位整机。
     * 对"配置错误"这类问题，复位并让故障可见比悄悄返回一个假读数更合适。
     */
    ADC1->CR2 |= ADC_CR2_SWSTART;

    while (!(ADC1->SR & ADC_SR_EOC)) { }
    return (uint16_t)(ADC1->DR & 0x0FFFu);
}

void board_pwm_set(sg_pwm_ch_t ch, uint16_t duty_permille)
{
    uint32_t ccr;

    if (duty_permille > 1000u) duty_permille = 1000u;
    ccr = (uint32_t)duty_permille * PWM_ARR / 1000u;

    switch (ch) {
    case SG_PWM_PUMP:  TIM3->CCR1 = ccr; break;
    case SG_PWM_LIGHT: TIM3->CCR2 = ccr; break;
    default: break;
    }
}

/* ---------------- DHT22 单总线 ---------------- */

#define DHT22_PIN   (1u << 8)
#define DHT22_PORT  GPIOA

static void dht22_dir_out(void) { DHT22_PORT->MODER = (DHT22_PORT->MODER & ~(3u << 16)) | (1u << 16); }
static void dht22_dir_in(void)  { DHT22_PORT->MODER &= ~(3u << 16); }
static void dht22_low(void)     { DHT22_PORT->BSRR = (DHT22_PIN << 16); }
static void dht22_high(void)    { DHT22_PORT->BSRR = DHT22_PIN; }
static int  dht22_level(void)   { return (DHT22_PORT->IDR & DHT22_PIN) ? 1 : 0; }

/*
 * 时序：主机拉低 >1ms 发起始信号 -> 释放 -> 传感器回 80us 低 + 80us 高 ->
 * 40 个 bit，每 bit 以 50us 低电平开始，随后的高电平 26-28us 表示 0、70us 表示 1。
 *
 * 两个实现上的要点：
 *
 * 1) 判 0 还是 1，量的是**时间**不是循环圈数。
 *    循环一次几周期取决于优化等级和是否内联，写死一个"跑 40 圈以上算 1"
 *    的阈值，换一次 -O 就会把所有 bit 都读成 1（40 圈在 168MHz 下只需要
 *    0.2us 左右，远小于最短的 26us 高电平）。改用 DWT 计周期之后，
 *    阈值可以按微秒给，跨编译选项稳定：
 *        0 -> 26-28us (约 4400-4700 周期)
 *        1 -> 70us    (约 11800 周期)
 *    取 50us 作为分界，两边各有约 25% 的余量。
 *
 * 2) 读期间关中断。整段约 5ms（起始 1.2ms + 响应 160us + 40bit x 约 120us），
 *    任何一次被中断插进来都会把电平宽度读错，40 个 bit 错一个校验和就过不了。
 *    代价是这 5ms 里 SysTick 不响应，FreeRTOS 的 tick 会往后漂几次，
 *    秒级的时间尺度下可以接受。
 *    也正因为关着中断，起始低电平用的是 delay_us()（DWT 忙等）而**不是**
 *    HAL_Delay()：后者的计数靠 SysTick 中断推进，在临界区里会死等。
 */

#define DHT22_TIMEOUT_US  200u    /* 单步等待上限（正常最长的是 80us 的响应脉冲） */
#define DHT22_ONE_US      50u     /* 高电平超过这个宽度判为 1 */
#define DHT22_START_US    1200u   /* 起始低电平，规格要求 >1ms */

static uint32_t dht22_us_to_cycles(uint32_t us)
{
    return us * (SystemCoreClock / 1000000u);
}

/*
 * 等数据线变成 level，返回等待消耗的周期数。
 * 超时（一直没变成 level）时返回的值会略大于 DHT22_TIMEOUT_US 对应的周期数，
 * 调用方用 > limit 就能判定超时。
 */
static uint32_t dht22_wait_until(int level)
{
    uint32_t t0    = DWT->CYCCNT;
    uint32_t limit = dht22_us_to_cycles(DHT22_TIMEOUT_US);

    while (dht22_level() != level) {
        if ((DWT->CYCCNT - t0) > limit) break;
    }
    return (uint32_t)(DWT->CYCCNT - t0);
}

static int dht22_capture(uint8_t raw[DHT22_RAW_LEN])
{
    uint32_t primask, limit = dht22_us_to_cycles(DHT22_TIMEOUT_US);
    int i, b;

    for (i = 0; i < DHT22_RAW_LEN; i++) raw[i] = 0;

    primask = __get_PRIMASK();
    __disable_irq();

    dht22_dir_out();
    dht22_low();
    delay_us(DHT22_START_US);       /* 起始信号：拉低 >1ms */
    dht22_high();
    delay_us(30);                   /* 释放后等 20-40us 再切输入，避开总线争用 */
    dht22_dir_in();

    /* 传感器拉低作为应答。一直为高说明没有接传感器或器件已坏 */
    dht22_wait_until(0);
    if (dht22_level() != 0) { __set_PRIMASK(primask); return -1; }

    dht22_wait_until(1);            /* 80us 应答低电平结束 */

    for (i = 0; i < DHT22_RAW_LEN; i++) {
        for (b = 7; b >= 0; b--) {
            uint32_t high_cyc;

            dht22_wait_until(0);            /* 这一位打头的 50us 低电平 */
            dht22_wait_until(1);            /* 低电平结束，高电平开始 */
            high_cyc = dht22_wait_until(0); /* 高电平持续了多久 —— 0/1 的判据 */

            /* 等不到下降沿说明传输被截断（拔线、上电过程中读到一半），
               此时数据不可信，直接放弃，让校验和那一层去兜底更危险 */
            if (high_cyc > limit) { __set_PRIMASK(primask); return -1; }

            raw[i] = (uint8_t)(raw[i] << 1);
            if (high_cyc > dht22_us_to_cycles(DHT22_ONE_US)) raw[i] |= 1u;
        }
    }

    __set_PRIMASK(primask);
    return 0;
}

int board_dht22_read(uint16_t *temp_x10, uint16_t *humi_x10)
{
    uint8_t raw[DHT22_RAW_LEN];

    if (!temp_x10 || !humi_x10) return -1;
    if (dht22_capture(raw) != 0) return -1;

    return dht22_decode(raw, temp_x10, humi_x10);   /* 校验和在这里判 */
}

/* ---------------- 内部 Flash 参数区 ---------------- */

int board_flash_read(uint32_t offset, void *buf, uint32_t len)
{
    const uint8_t *src;
    uint32_t i;

    if (!buf) return -1;
    if (offset + len > FLASH_SECTOR_SIZE) return -1;

    src = (const uint8_t *)(FLASH_PARAM_ADDR + offset);
    for (i = 0; i < len; i++) ((uint8_t *)buf)[i] = src[i];
    return 0;
}

int board_flash_write(uint32_t offset, const void *buf, uint32_t len)
{
    const uint32_t *p = (const uint32_t *)buf;
    uint32_t words = (len + 3u) / 4u;
    uint32_t i;
    FLASH_EraseInitTypeDef er;
    uint32_t sector_err = 0;

    if (!buf) return -1;
    if (offset + len > FLASH_SECTOR_SIZE) return -1;

    HAL_FLASH_Unlock();

    /* 扇区是擦除的最小单位，所以改参数要整扇区擦掉再写回
       （参数区只有几十字节，占用的是专门的最后一个扇区，不影响程序区） */
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    er.Sector       = FLASH_SECTOR;
    er.NbSectors    = 1;
    if (HAL_FLASHEx_Erase(&er, &sector_err) != HAL_OK) {
        HAL_FLASH_Lock();
        return -2;
    }

    for (i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              FLASH_PARAM_ADDR + offset + i * 4u,
                              (uint64_t)p[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return -3;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

/* ---------------- 独立看门狗 ---------------- */

static IWDG_HandleTypeDef g_iwdg;

void board_watchdog_init(uint32_t timeout_ms)
{
    /*
     * IWDG 的时钟源是 LSI（约 32kHz）—— 它是独立于系统时钟的 RC，
     * 所以主频跑飞 / PLL 失锁时看门狗仍然有效，这正是选它而不是 WWDG 的原因。
     * 但 LSI 需要显式打开并等它稳定，否则 HAL_IWDG_Init 写进去的分频值
     * 会在一个还没起振的时钟上生效。
     */
    RCC->CSR |= RCC_CSR_LSION;
    while (!(RCC->CSR & RCC_CSR_LSIRDY)) { }

    /*
     * 32 分频后计数周期约 1ms，所以 Reload 可以直接按毫秒给：
     *   T = Reload x 32 / 32000 = Reload / 1000 (秒)
     * 传 4000 就是 4 秒。
     */
    g_iwdg.Instance       = IWDG;
    g_iwdg.Init.Prescaler = IWDG_PRESCALER_32;
    g_iwdg.Init.Reload    = timeout_ms;
    HAL_IWDG_Init(&g_iwdg);
}

void board_watchdog_feed(void)
{
    HAL_IWDG_Refresh(&g_iwdg);
}

/* ---------------- BLE（UART 透传模块，USART2） ---------------- */

/* huart2 的定义与初始化在 firmware/uart_ble.c */
extern UART_HandleTypeDef huart2;

int board_ble_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return -1;
    return (HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 20) == HAL_OK) ? 0 : -1;
}

int board_ble_recv(uint8_t *buf, uint16_t max_len)
{
    uint16_t got = 0;

    if (!buf || max_len == 0) return 0;

    /*
     * 非阻塞取字节。用超时 0 的 HAL_UART_Receive 会直接失败，
     * 所以这里按"一个字节一个字节地试"的方式取，取到就继续，取不到就返回。
     * 这样完全靠用户定时器，不需要开 UART 中断。
     */
    while (got < max_len) {
        if (HAL_UART_Receive(&huart2, &buf[got], 1, 0) != HAL_OK) break;
        got++;
    }
    return (int)got;
}

/* ---------------- 时间 ---------------- */

void board_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

uint32_t board_millis(void)
{
    return HAL_GetTick();
}
