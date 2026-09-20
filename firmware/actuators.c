#include "actuators.h"
#include "board.h"

/*
 * 执行机构：水泵与补光灯都由 PWM 驱动。
 * 占空比统一用千分比（0-1000）表示，避免上层到处出现浮点；
 * 板级层负责把它换算成定时器比较值（见 board_stm32f4.c）。
 */

static uint16_t g_duty[SG_PWM_CH_COUNT];

void actuators_init(void)
{
    g_duty[SG_PWM_PUMP]  = 0;
    g_duty[SG_PWM_LIGHT] = 0;
    board_pwm_set(SG_PWM_PUMP,  0);
    board_pwm_set(SG_PWM_LIGHT, 0);
}

void actuators_apply(const sg_act_t *act)
{
    uint16_t pump, light;

    if (!act) return;

    pump  = (act->pump  > 1000u) ? 1000u : act->pump;
    light = (act->light > 1000u) ? 1000u : act->light;

    g_duty[SG_PWM_PUMP]  = pump;
    g_duty[SG_PWM_LIGHT] = light;

    board_pwm_set(SG_PWM_PUMP,  pump);
    board_pwm_set(SG_PWM_LIGHT, light);
}

void actuators_all_off(void)
{
    g_duty[SG_PWM_PUMP]  = 0;
    g_duty[SG_PWM_LIGHT] = 0;
    board_pwm_set(SG_PWM_PUMP,  0);
    board_pwm_set(SG_PWM_LIGHT, 0);
}

uint16_t actuators_get_duty(sg_pwm_ch_t ch)
{
    if (ch >= SG_PWM_CH_COUNT) return 0;
    return g_duty[ch];
}
