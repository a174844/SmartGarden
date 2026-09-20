#ifndef SG_FILTERS_H
#define SG_FILTERS_H

#include <stdint.h>
#include <stddef.h>

#define SG_MOVAVG_MAX 32

/* 滑动平均滤波器：抑制周期性噪声与电源纹波 */
typedef struct {
    uint16_t buf[SG_MOVAVG_MAX];
    size_t   idx;
    size_t   len;
    uint32_t sum;
} movavg_t;

void     movavg_init(movavg_t *f, size_t len);
uint16_t movavg_push(movavg_t *f, uint16_t v);

/* 中值滤波器：剔除脉冲型异常采样点 */
typedef struct {
    uint16_t win[SG_MOVAVG_MAX];
    size_t   idx;
    size_t   len;
    size_t   filled;
} medfilt_t;

void     medfilt_init(medfilt_t *f, size_t len);
uint16_t medfilt_push(medfilt_t *f, uint16_t v);

/* 二级滤波：先中值剔除脉冲，再滑动平均平滑 */
typedef struct {
    medfilt_t med;
    movavg_t  avg;
} sensor_filter_t;

void     sensor_filter_init(sensor_filter_t *f, size_t med_len, size_t avg_len);
uint16_t sensor_filter_push(sensor_filter_t *f, uint16_t raw);

#endif /* SG_FILTERS_H */
