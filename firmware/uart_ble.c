#include "uart_ble.h"

#include "stm32f4xx_hal.h"

/*
 * BLE 透传模块挂在 USART2 上：PA2 = USART2_TX，PA3 = USART2_RX，AF7。
 * 模块是"串口透传"型的（内部跑自己的 BLE 协议栈），主机侧只当普通串口用，
 * 所以这里只需要把串口配起来，不涉及 BLE 协议本身。
 *
 * 115200-8-N-1：常见透传模块的出厂默认，且必须在模块上电前就位 ——
 * 模块上电时会打印自己的状态，串口没配好的话那几行会丢。
 */

UART_HandleTypeDef huart2;

void ble_uart_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;   /* 模块未插时 RX 不悬空，避免收到随机噪声 */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    /*
     * 返回值不吞掉：串口配不起来（比如 PCLK 算不出合法 BRR）在这里就能发现，
     * 而不是等到 BLE 任务发不出帧、去查协议的时候才怀疑到这里。
     * 现场没有串口调试口，所以用挂起的方式暴露出问题。
     */
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        for (;;) { }
    }
}
