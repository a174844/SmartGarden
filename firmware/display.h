#ifndef SG_DISPLAY_H
#define SG_DISPLAY_H

#include <stdint.h>

#include "control.h"

/*
 * 状态屏（SSD1306 128x64，I2C）的板级封装。
 *
 * 注意：这里的几个函数都**不加锁**。显存和 I2C 总线是采集任务与监控任务
 * 共用的，调用方必须自己持有互斥量（main.c 里的 mtx_i2c），
 * 否则两次刷新的指令会交错到总线上，屏上会出现乱码。
 */

/* 上电初始化：发初始化指令流 + 画标题 + 整屏推一次 */
void display_init(void);

/* 环境数值与进度条（页 2-6），由采集任务调用 */
void display_update_env(const sg_env_t *env, const sg_act_t *act);

/* 模式 / 链路状态 / 运行时间（页 1 与页 7），由监控任务调用 */
void display_update_status(uint8_t mode, uint32_t hb_miss, uint32_t uptime_s);

#endif /* SG_DISPLAY_H */
