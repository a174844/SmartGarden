#ifndef SG_PANEL_H
#define SG_PANEL_H

#include <stdint.h>

#include "control.h"
#include "ssd1306.h"

/*
 * OLED 状态面板排版。
 *
 * 128x64 的屏分成 8 页（每页 8 像素高）。把每一页固定分给一个刷新来源，
 * 两个任务就能各刷各的、互不覆盖 —— 这也是互斥量的意义所在：
 *   页区间不同，但它们共用同一块显存和同一条 I2C 总线。
 *
 *   page 0   SMARTGARDEN           标题（上电画一次）
 *   page 1   AUTO  LINK OK         模式 + 链路（监控任务）
 *   page 2   T 25.3C H 61.2%       采集任务
 *   page 3   SOIL 45% LUX 3200     采集任务
 *   page 4   PUMP 62% LED 100%     采集任务
 *   page 5   MOIST ##########      采集任务
 *   page 6   LIGHT ##########      采集任务
 *   page 7   UP 0012345S           运行时间（监控任务）
 *
 * 所有文字一律用大写：字库只覆盖 ASCII 0x20-0x5F。
 */

#define PANEL_PAGE_TITLE   0u
#define PANEL_PAGE_MODE    1u
#define PANEL_ENV_FIRST    2u
#define PANEL_ENV_LAST     6u
#define PANEL_PAGE_UPTIME  7u

/* 光照进度条的满量程（lux）。只用于显示，不参与控制决策。
   光敏电阻的换算本来就是粗标定，这里给的是一个"看着合理"的满量程。 */
#define PANEL_LUX_FULL_SCALE 20000u

/* 标题与分隔线，上电时画一次 */
void panel_render_title(ssd1306_t *d);

/* 环境数值与两条进度条（页 2-6） */
void panel_render_env(ssd1306_t *d, const sg_env_t *env, const sg_act_t *act);

/* 工作模式与链路状态（页 1） */
void panel_render_mode(ssd1306_t *d, uint8_t mode, uint32_t hb_miss);

/* 运行时间（页 7） */
void panel_render_uptime(ssd1306_t *d, uint32_t uptime_s);

#endif /* SG_PANEL_H */
