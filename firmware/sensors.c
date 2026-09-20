#include "sensors.h"
#include "filters.h"
#include "board.h"

/*
 * ADC 原始值 -> 工程量的换算。
 *
 * 这里的标定常数是按典型器件给的初值，量产时需要按实际探头逐台校准 ——
 * 所以把校准偏移做成了参数（存在 Flash 里，见 nvstore），
 * 上层可以通过 App 下发校准值，不必重新烧固件。
 */

/* 探头式土壤湿度计：干燥时输出约 3000mV，浸在水中约 1200mV，中间线性 */
#define SOIL_MV_DRY  3000u
#define SOIL_MV_WET  1200u

/* 光敏电阻分压：照度与阻值高度非线性，这里用分段线性近似，
   系数取的是"室内 300lux 左右量级可用"的粗标定值 */
#define LIGHT_LUX_PER_MV 10u

#define ADC_MAX     4095u
#define ADC_VREF_MV 3300u

uint16_t soil_raw_to_percent(uint16_t raw, int16_t cal)
{
    uint32_t mv;
    int32_t  pct;

    mv = (uint32_t)raw * ADC_VREF_MV / (ADC_MAX + 1u);

    /* 电压越低含水量越高 */
    pct = (int32_t)(SOIL_MV_DRY - mv) * 100 / (int32_t)(SOIL_MV_DRY - SOIL_MV_WET);
    pct += cal;

    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (uint16_t)pct;
}

uint16_t light_raw_to_lux(uint16_t raw, int16_t cal)
{
    uint32_t mv;
    int32_t  lux;

    mv  = (uint32_t)raw * ADC_VREF_MV / (ADC_MAX + 1u);
    lux = (int32_t)mv * LIGHT_LUX_PER_MV;
    lux += cal;

    if (lux < 0)     lux = 0;
    if (lux > 65535) lux = 65535;
    return (uint16_t)lux;
}

/*
 * 二级滤波：先中值剔脉冲，再滑动平均平滑。
 * 加滤波的原因是水泵与补光灯都是 PWM 驱动的感性/大电流负载，启停时
 * 会在 3.3V 上叠出纹波，直接读 ADC 会出现几十个百分点的跳变。
 */
static sensor_filter_t f_soil, f_light;
static int             f_inited;

void sensors_prime(void)
{
    sensor_filter_init(&f_soil,  5, 8);   /* 中值 5 点 + 滑动平均 8 点 */
    sensor_filter_init(&f_light, 5, 8);

    /* 用当前读数预填窗口：否则窗口初始全 0，头几次输出会被 0 拉低，
       表现为"土壤突然变成 0%（极干）"，控制任务会立刻把水泵打到满速 */
    {
        uint16_t raw = board_adc_read(SG_ADC_SOIL);
        uint8_t  i;
        for (i = 0; i < 8; i++) sensor_filter_push(&f_soil, soil_raw_to_percent(raw, 0));
    }
    {
        uint16_t raw = board_adc_read(SG_ADC_LIGHT);
        uint8_t  i;
        for (i = 0; i < 8; i++) sensor_filter_push(&f_light, light_raw_to_lux(raw, 0));
    }

    f_inited = 1;
}

int dht22_read(uint16_t *temp_x10, uint16_t *humi_x10)
{
    return board_dht22_read(temp_x10, humi_x10);
}

void sensors_sample(sg_env_t *out)
{
    uint16_t t = 0, h = 0;
    uint16_t raw;

    if (!out) return;
    if (!f_inited) sensors_prime();

    /* DHT22 读失败时保留上一次的值，不把 0 写进去 ——
       温度 0℃ 会让上层误判为"环境异常" */
    if (dht22_read(&t, &h) == 0) {
        out->temp = t;
        out->humi = h;
    }

    raw = board_adc_read(SG_ADC_SOIL);
    out->soil = sensor_filter_push(&f_soil, soil_raw_to_percent(raw, 0));

    raw = board_adc_read(SG_ADC_LIGHT);
    out->light = sensor_filter_push(&f_light, light_raw_to_lux(raw, 0));
}
