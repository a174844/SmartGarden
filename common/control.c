#include "control.h"

/*
 * 缺水/缺光程度 -> 执行器占空比映射。
 * value >= low   : 处于滞回区间，维持中等输出，避免在阈值附近频繁启停
 * value == 0     : 缺口最大，满占空比
 * 两者之间       : 按缺口深度线性加大
 */
static uint16_t deficit_to_duty(uint16_t value, uint16_t low)
{
    uint32_t deficit, duty;

    if (value >= low) return 300;
    if (low == 0)     return 1000;

    deficit = (uint32_t)(low - value);
    duty    = 300u + (deficit * 700u) / (uint32_t)low;
    if (duty > 1000u) duty = 1000u;
    return (uint16_t)duty;
}

static uint16_t light_to_duty(uint16_t lux, uint16_t low)
{
    uint32_t deficit, duty;

    if (lux >= low) return 200;
    if (low == 0)   return 1000;

    deficit = (uint32_t)(low - lux);
    duty    = 200u + (deficit * 800u) / (uint32_t)low;
    if (duty > 1000u) duty = 1000u;
    return (uint16_t)duty;
}

void sg_control_update(const sg_env_t *env, const sg_params_t *th, sg_act_t *out)
{
    if (env->soil >= th->soil_high)
        out->pump = 0;
    else
        out->pump = deficit_to_duty(env->soil, th->soil_low);

    if (env->light >= th->light_high)
        out->light = 0;
    else
        out->light = light_to_duty(env->light, th->light_low);
}

void sg_control_apply_manual(const sg_cmd_t *cmd, sg_act_t *out)
{
    if (!cmd || !out) return;

    out->pump  = (cmd->pump  > 1000u) ? 1000u : cmd->pump;
    out->light = (cmd->light > 1000u) ? 1000u : cmd->light;
}
