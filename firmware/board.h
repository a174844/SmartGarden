#ifndef SG_BOARD_H
#define SG_BOARD_H

#include <stdint.h>

/*
 * 板级抽象层。
 *
 * 把「碰寄存器/碰外设」的部分集中到这里，好处是控制与协议逻辑
 * （control / ble_frame / dht22 / nvstore）完全不依赖 STM32 头文件，
 * 既方便换 MCU，也方便单独验证上层逻辑，不必每次都把板子接上。
 */

typedef enum {
    SG_ADC_SOIL  = 0,   /* 探头式土壤湿度传感器 */
    SG_ADC_LIGHT = 1,   /* 光敏电阻分压 */
    SG_ADC_CH_COUNT
} sg_adc_ch_t;

typedef enum {
    SG_PWM_PUMP  = 0,   /* 水泵，TIM3_CH1 */
    SG_PWM_LIGHT = 1,   /* 补光，TIM3_CH2 */
    SG_PWM_CH_COUNT
} sg_pwm_ch_t;

int      board_init(void);

/* ADC 单次采样，12bit 原始值 0-4095 */
uint16_t board_adc_read(sg_adc_ch_t ch);

/* PWM 占空比，千分比 0-1000 */
void     board_pwm_set(sg_pwm_ch_t ch, uint16_t duty_permille);

/*
 * 心跳指示灯（PD0，低电平点亮）。
 * 监控任务每轮翻转一次，用它判断"固件还在跑"而不是"屏还亮着"——
 * 屏在调试期间经常是不插的。
 */
void     board_led_set(int on);

/* 读一次 DHT22。成功返回 0，并把温湿度写入参数（单位 0.1） */
int      board_dht22_read(uint16_t *temp_x10, uint16_t *humi_x10);

/*
 * I2C 总线（状态屏 OLED，7 位从机地址）。
 * 这一层只管"把这段字节发出去"，不负责加锁：
 * 总线上可能同时有多个使用者，串行化由上层用互斥量完成（见 main.c 的 mtx_i2c）。
 * 返回 0 成功，非 0 表示从机无应答或总线超时（已发 STOP 释放总线）。
 */
int      board_i2c_write(uint8_t addr, const uint8_t *buf, uint16_t len);

/* 内部 Flash 参数区。offset 从参数区起始算起 */
int      board_flash_read (uint32_t offset, void *buf, uint32_t len);
int      board_flash_write(uint32_t offset, const void *buf, uint32_t len);

/* 独立看门狗 */
void     board_watchdog_init(uint32_t timeout_ms);
void     board_watchdog_feed(void);

/*
 * BLE 链路。硬件上是一颗 UART 透传的 BLE 模块：
 * board_ble_send 把整帧写进 UART，模块负责空口收发；
 * board_ble_recv 从 UART 取回已经到达的字节（非阻塞，0 表示暂时没有）。
 * 帧的切分由 common/ble_frame.c 的字节流组帧器负责，
 * 因为透传模块不保证一次给的正好是一帧。
 */
int      board_ble_send(const uint8_t *data, uint16_t len);
int      board_ble_recv(uint8_t *buf, uint16_t max_len);

void     board_delay_ms(uint32_t ms);
uint32_t board_millis(void);

#endif /* SG_BOARD_H */
