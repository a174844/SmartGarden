#include <string.h>
#include "ble_frame.h"

uint8_t sg_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    size_t  i;
    int     b;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            /* 写成"先移位、再按最高位决定是否异或"，而不是 ?: 表达式：
               ?: 两个分支一个是有符号一个是无符号时 -Wextra 会告警，
               而且拆开写更能看出这就是标准 CRC-8（多项式 0x07）的逐位算法 */
            uint8_t msb = (uint8_t)(crc & 0x80u);

            crc = (uint8_t)(crc << 1);
            if (msb) crc ^= 0x07u;
        }
    }
    return crc;
}

size_t sg_ble_build(uint8_t type, const uint8_t *payload, uint8_t plen,
                    uint8_t *out, size_t out_max)
{
    size_t total;

    if (!out) return 0;
    if (plen > SG_BLE_PLEN_MAX) return 0;
    if (plen > 0 && !payload) return 0;

    total = (size_t)plen + 4u;    /* SOF + TYPE + LEN + payload + CRC8 */
    if (out_max < total) return 0;

    out[0] = SG_BLE_SOF;
    out[1] = type;
    out[2] = plen;
    if (plen) memcpy(&out[3], payload, plen);
    out[3 + plen] = sg_crc8(&out[1], (size_t)plen + 2u);   /* 覆盖 TYPE+LEN+PAYLOAD */

    return total;
}

int sg_ble_parse(const uint8_t *frame, size_t len,
                 uint8_t *type, const uint8_t **payload)
{
    uint8_t plen;

    if (!frame || !type) return -1;
    if (len < 4u) return -1;
    if (frame[0] != SG_BLE_SOF) return -2;

    plen = frame[2];
    if (plen > SG_BLE_PLEN_MAX) return -3;
    if (len != (size_t)plen + 4u) return -3;        /* 长度必须精确匹配，不收半帧 */

    if (sg_crc8(&frame[1], (size_t)plen + 2u) != frame[3 + plen]) return -4;

    *type = frame[1];
    if (payload) *payload = &frame[3];              /* payload 允许为 NULL，仅校验时用 */
    return (int)plen;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

size_t sg_ble_build_env(const sg_env_t *env, const sg_act_t *act,
                        uint8_t *out, size_t out_max)
{
    uint8_t p[SG_BLE_ENV_PLEN];

    if (!env || !act) return 0;

    put_u16(&p[0],  env->temp);
    put_u16(&p[2],  env->humi);
    put_u16(&p[4],  env->soil);
    put_u16(&p[6],  env->light);
    put_u16(&p[8],  act->pump);
    put_u16(&p[10], act->light);

    return sg_ble_build(SG_BLE_T_ENV, p, SG_BLE_ENV_PLEN, out, out_max);
}

size_t sg_ble_build_heartbeat(uint8_t *out, size_t out_max)
{
    return sg_ble_build(SG_BLE_T_HB, NULL, 0u, out, out_max);
}

int sg_ble_parse_cmd(const uint8_t *payload, uint8_t plen, sg_cmd_t *cmd)
{
    if (!payload || !cmd) return -1;
    if (plen != SG_BLE_CMD_PLEN) return -1;

    cmd->mode  = payload[0];
    if (cmd->mode != SG_MODE_AUTO && cmd->mode != SG_MODE_MANUAL) return -2;

    cmd->pump  = get_u16(&payload[1]);
    cmd->light = get_u16(&payload[3]);

    /* 占空比是千分比，越界直接拒收而不是截断：
       截断会让 App 以为生效了，实际输出与下发值不一致 */
    if (cmd->pump > 1000u || cmd->light > 1000u) return -3;

    return 0;
}

/* ---------------- 字节流组帧 ---------------- */

void sg_ble_rx_reset(sg_ble_rx_t *rx)
{
    if (!rx) return;
    rx->len = 0;
}

const uint8_t *sg_ble_rx_payload(const sg_ble_rx_t *rx)
{
    return rx ? &rx->buf[3] : NULL;
}

int sg_ble_rx_push(sg_ble_rx_t *rx, uint8_t byte, uint8_t *type, uint8_t *plen)
{
    uint8_t t, l;

    if (!rx || !type || !plen) return 0;

    /* 缓冲区满还没成帧，说明前面已经乱了，丢掉重来 */
    if (rx->len >= SG_BLE_FRAME_MAX) rx->len = 0;

    rx->buf[rx->len++] = byte;

    /* 先对齐 SOF */
    while (rx->len > 0 && rx->buf[0] != SG_BLE_SOF) {
        memmove(rx->buf, &rx->buf[1], --rx->len);
    }

    if (rx->len < 3u) return 0;                 /* 还没拿到 LEN */

    l = rx->buf[2];
    if (l > SG_BLE_PLEN_MAX) {                  /* LEN 非法：丢掉这个 SOF 重新找 */
        memmove(rx->buf, &rx->buf[1], --rx->len);
        return 0;
    }

    if (rx->len < (size_t)l + 4u) return 0;     /* 帧还没收全 */

    {
        int r = sg_ble_parse(rx->buf, (size_t)l + 4u, &t, NULL);
        rx->len = 0;                            /* 无论成败都从下一字节重新开始 */
        if (r < 0) return 0;
    }

    *type = t;
    *plen = l;
    return (int)l + 4;
}
