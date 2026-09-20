#ifndef SG_CONTROL_H
#define SG_CONTROL_H

#include <stdint.h>
#include "nvstore.h"

typedef struct {
    uint16_t soil;   /* 土壤湿度(%) */
    uint16_t light;  /* 光照(lux) */
    uint16_t temp;   /* 温度(0.1℃) */
    uint16_t humi;   /* 湿度(0.1%) */
} sg_env_t;

typedef struct {
    uint16_t pump;   /* 水泵 PWM 占空比(千分比 0-1000) */
    uint16_t light;  /* 补光 PWM 占空比(千分比 0-1000) */
} sg_act_t;

/* 工作模式 */
#define SG_MODE_AUTO   0u   /* 按阈值自动闭环 */
#define SG_MODE_MANUAL 1u   /* 按 App 下发的占空比执行 */

/* App 下发的控制指令 */
typedef struct {
    uint8_t  mode;    /* SG_MODE_AUTO / SG_MODE_MANUAL */
    uint16_t pump;
    uint16_t light;
} sg_cmd_t;

/* 根据阈值与环境值计算执行器输出，带滞回以避免在阈值附近频繁启停 */
void sg_control_update(const sg_env_t *env, const sg_params_t *th, sg_act_t *out);

/*
 * 手动模式下直接采用 App 下发的占空比。
 * 单独抽出来是为了让控制任务的分支足够显式：手动值一律做上限钳位，
 * 防止 App 下发越界值把水泵/补光灯打到超范围。
 */
void sg_control_apply_manual(const sg_cmd_t *cmd, sg_act_t *out);

#endif
