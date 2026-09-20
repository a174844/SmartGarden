#ifndef SG_DHT22_H
#define SG_DHT22_H

#include <stdint.h>

/*
 * DHT22 数据帧的解码部分。单总线时序的采集在板级层（board_dht22_read），
 * 这里只做纯逻辑：校验和校验 + 原始值换算成工程量。
 *
 * 拆开的目的是让解码这一半能在 PC 上单独测（构造正常帧、篡改校验和、
 * 边界温湿度等），不必依赖真实的微秒级时序。
 */

#define DHT22_RAW_LEN 5

/*
 * 输入 5 字节原始帧（湿度高/低、温度高/低、校验和），
 * 校验通过则输出温度与湿度（均为 0.1 为单位），返回 0；
 * 校验失败返回 -1，参数不被修改。
 *
 * 温度最高位为符号位：值为负时按二进制补码解释。
 */
int dht22_decode(const uint8_t raw[DHT22_RAW_LEN],
                 uint16_t *temp_x10, uint16_t *humi_x10);

/* 同上，但温度以有符号返回，便于上层直接判断 */
int dht22_decode_signed(const uint8_t raw[DHT22_RAW_LEN],
                        int16_t *temp_x10, uint16_t *humi_x10);

#endif /* SG_DHT22_H */
