#ifndef SG_UART_BLE_H
#define SG_UART_BLE_H

/*
 * BLE 透传模块所在的串口（USART2，PA2=TX / PA3=RX）。
 *
 * 单独一个文件是因为 board_stm32f4.c 里只写了 `extern UART_HandleTypeDef huart2;`
 * 就当成"已初始化"，而实际上从来没有哪份代码定义并初始化过它 ——
 * 那样的代码能编译过，但一调用 HAL_UART_Transmit 就是在解引用空指针。
 */
void ble_uart_init(void);

#endif /* SG_UART_BLE_H */
