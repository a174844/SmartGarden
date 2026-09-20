#ifndef SG_ACTUATORS_H
#define SG_ACTUATORS_H

#include <stdint.h>
#include "board.h"
#include "control.h"

void     actuators_init(void);

/* 下发占空比（千分比），内部做上限钳位 */
void     actuators_apply(const sg_act_t *act);

/* 紧急关断，用于心跳超时等异常场景 */
void     actuators_all_off(void);

/* 回读当前占空比，供状态屏显示使用 */
uint16_t actuators_get_duty(sg_pwm_ch_t ch);

#endif
