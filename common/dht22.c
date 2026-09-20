#include "dht22.h"

/*
 * DHT22 数据帧：5 字节
 *   [0..1] 湿度，16bit 大端，单位 0.1%
 *   [2..3] 温度，16bit 大端，单位 0.1℃；最高位为 1 时表示负温度，其余位为补码
 *   [4]    校验和 = 前四字节之和的低 8 位
 */

int dht22_decode_signed(const uint8_t raw[DHT22_RAW_LEN],
                        int16_t *temp_x10, uint16_t *humi_x10)
{
    uint8_t  sum;
    uint16_t h, t;

    if (!raw) return -1;

    sum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (sum != raw[4]) return -1;

    h = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    t = (uint16_t)(((uint16_t)raw[2] << 8) | raw[3]);

    if (t & 0x8000u) {
        /* 负温度：低 15 位是补码 */
        t &= 0x7FFFu;
        *temp_x10 = (int16_t)(-(int16_t)t);
    } else {
        *temp_x10 = (int16_t)t;
    }

    *humi_x10 = h;
    return 0;
}

int dht22_decode(const uint8_t raw[DHT22_RAW_LEN],
                 uint16_t *temp_x10, uint16_t *humi_x10)
{
    int16_t t;

    if (!temp_x10 || !humi_x10) return -1;
    if (dht22_decode_signed(raw, &t, humi_x10) != 0) return -1;

    *temp_x10 = (t < 0) ? 0u : (uint16_t)t;
    return 0;
}
