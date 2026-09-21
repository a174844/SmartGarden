#include <string.h>
#include "nvstore.h"
#include "crc32.h"

void sg_params_default(sg_params_t *p)
{
    p->soil_low      = 30;
    p->soil_high     = 60;
    p->light_low     = 2000;
    p->light_high    = 8000;
    p->soil_cal      = 0;
    p->light_cal     = 0;
    /*
     * 2000ms 而不是 1000ms：DHT22 手册要求两次读取间隔不小于 2 秒，
     * 采得比它快时器件会直接回上一次的旧值（甚至回错误帧）。
     * 土壤湿度/光照这种量本身变化很慢，2 秒一次的响应速度对灌溉完全够用。
     */
    p->sample_period = 2000;
}

size_t sg_params_serialize(const sg_params_t *p, uint8_t *out, size_t out_max)
{
    sg_params_block_t blk;
    if (out_max < sizeof(sg_params_block_t)) return 0;

    blk.params = *p;
    blk.crc    = crc32_calc(&blk.params, sizeof(sg_params_t));
    memcpy(out, &blk, sizeof(blk));
    return sizeof(blk);
}

int sg_params_deserialize(const uint8_t *in, size_t len, sg_params_t *p)
{
    sg_params_block_t blk;

    if (len < sizeof(sg_params_block_t)) return 0;

    memcpy(&blk, in, sizeof(blk));

    /* CRC 校验失败说明掉电写入不完整或数据错乱，拒绝加载，由调用方回落默认值 */
    if (blk.crc != crc32_calc(&blk.params, sizeof(sg_params_t))) return 0;

    *p = blk.params;
    return 1;
}
