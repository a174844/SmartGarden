#ifndef SG_NVSTORE_H
#define SG_NVSTORE_H

#include <stdint.h>
#include <stddef.h>   /* size_t：下面两个序列化接口的签名要用。
                         之前只包了 <stdint.h>，在 PC 上编得过是因为别的头
                         文件先把 size_t 带进来了；换成 freestanding 的
                         ARM 构建就暴露成 unknown type name。 */

/* 存入内部 Flash 的阈值与校准参数 */
typedef struct {
    uint16_t soil_low;      /* 土壤湿度下限(%)，低于此值启动灌溉 */
    uint16_t soil_high;     /* 土壤湿度上限(%)，高于此值停止灌溉 */
    uint16_t light_low;     /* 光照下限(lux)，低于此值启动补光 */
    uint16_t light_high;    /* 光照上限(lux)，高于此值停止补光 */
    int16_t  soil_cal;      /* 土壤湿度校准偏移 */
    int16_t  light_cal;     /* 光照校准偏移 */
    uint16_t sample_period; /* 采集周期(ms) */
} sg_params_t;

typedef struct {
    sg_params_t params;
    uint32_t    crc;        /* 对 params 结构体计算的 CRC32 */
} sg_params_block_t;

void sg_params_default(sg_params_t *p);

/* 序列化/反序列化：把 params+crc 打包成可写入 Flash 的字节流 */
size_t sg_params_serialize(const sg_params_t *p, uint8_t *out, size_t out_max);
int    sg_params_deserialize(const uint8_t *in, size_t len, sg_params_t *p);

#endif
