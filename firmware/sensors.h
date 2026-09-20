#ifndef SG_SENSORS_H
#define SG_SENSORS_H

#include <stdint.h>
#include "control.h"

/* 读一次 DHT22（单总线时序在板级层，解码在 common/dht22.c）。成功返回 0 */
int dht22_read(uint16_t *temp_x10, uint16_t *humi_x10);

/* ADC 原始值 -> 工程量（标定常数见 sensors.c，可按探头校准） */
uint16_t soil_raw_to_percent(uint16_t raw, int16_t cal);
uint16_t light_raw_to_lux(uint16_t raw, int16_t cal);

/* 采集一次完整环境数据：DHT22 + 两路 ADC，并对土壤/光照做二级滤波 */
void sensors_sample(sg_env_t *out);

/* 用当前读数预填滤波窗口，避免启动瞬间均值被 0 拉偏 */
void sensors_prime(void);

#endif
