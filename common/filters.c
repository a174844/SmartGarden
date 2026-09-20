#include "filters.h"

void movavg_init(movavg_t *f, size_t len)
{
    size_t i;
    if (len > SG_MOVAVG_MAX) len = SG_MOVAVG_MAX;
    if (len == 0) len = 1;
    for (i = 0; i < SG_MOVAVG_MAX; i++) f->buf[i] = 0;
    f->idx = 0;
    f->len = len;
    f->sum = 0;
}

uint16_t movavg_push(movavg_t *f, uint16_t v)
{
    /* 减去即将被覆盖的旧值，再加上新值，保持 O(1) */
    f->sum -= f->buf[f->idx];
    f->buf[f->idx] = v;
    f->sum += v;
    f->idx = (f->idx + 1) % f->len;
    return (uint16_t)(f->sum / f->len);
}

void medfilt_init(medfilt_t *f, size_t len)
{
    size_t i;
    if (len > SG_MOVAVG_MAX) len = SG_MOVAVG_MAX;
    if (len == 0) len = 1;
    for (i = 0; i < SG_MOVAVG_MAX; i++) f->win[i] = 0;
    f->idx = 0;
    f->len = len;
    f->filled = 0;
}

uint16_t medfilt_push(medfilt_t *f, uint16_t v)
{
    size_t i, n;
    uint16_t tmp[SG_MOVAVG_MAX];

    f->win[f->idx] = v;
    f->idx = (f->idx + 1) % f->len;
    if (f->filled < f->len) f->filled++;

    n = f->filled;
    for (i = 0; i < n; i++) tmp[i] = f->win[i];

    /* 插入排序取中位数，窗口小，开销可接受 */
    for (i = 1; i < n; i++) {
        uint16_t key = tmp[i];
        size_t j = i;
        while (j > 0 && tmp[j - 1] > key) {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = key;
    }
    return tmp[n / 2];
}

void sensor_filter_init(sensor_filter_t *f, size_t med_len, size_t avg_len)
{
    medfilt_init(&f->med, med_len);
    movavg_init(&f->avg, avg_len);
}

uint16_t sensor_filter_push(sensor_filter_t *f, uint16_t raw)
{
    return movavg_push(&f->avg, medfilt_push(&f->med, raw));
}
