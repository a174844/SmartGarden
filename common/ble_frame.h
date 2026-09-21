#ifndef SG_BLE_FRAME_H
#define SG_BLE_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include "control.h"

/*
 * BLE 帧格式（与 docs/BLE协议.md 一致）：
 *
 *   | SOF | TYPE | LEN | PAYLOAD | CRC8 |
 *     1B    1B    1B    0-20B     1B
 *
 *   SOF   = 0xAA
 *   CRC8 覆盖 TYPE + LEN + PAYLOAD（多项式 0x07，初值 0x00，不取反）
 *
 * 组帧与解析都放在 common/ 下：它们不碰任何硬件，可以在 PC 上完整测试，
 * 包括非法帧、长度越界、CRC 错误、粘包等边界情况。
 */

#define SG_BLE_SOF      0xAAu
#define SG_BLE_PLEN_MAX 20u
#define SG_BLE_FRAME_MAX (4u + SG_BLE_PLEN_MAX)   /* SOF+TYPE+LEN+payload+CRC8 */

/* 帧类型 */
#define SG_BLE_T_ENV  0x01u   /* 设备 -> App：环境数据 */
#define SG_BLE_T_CMD  0x02u   /* App -> 设备：控制指令 */
#define SG_BLE_T_HB   0x03u   /* 双向：心跳 */

/* 环境数据 payload 长度：温度2 + 湿度2 + 土壤2 + 光照2 + 泵2 + 补光2 */
#define SG_BLE_ENV_PLEN 12u
/* 控制指令 payload 长度：模式1 + 泵2 + 补光2 */
#define SG_BLE_CMD_PLEN 5u

uint8_t sg_crc8(const uint8_t *data, size_t len);

/* 组帧。返回帧总长，0 表示参数非法或缓冲区不够 */
size_t sg_ble_build(uint8_t type, const uint8_t *payload, uint8_t plen,
                    uint8_t *out, size_t out_max);

/*
 * 解析一帧。返回 payload 长度（>=0），负值表示不合法：
 *   -1 长度不足  -2 SOF 不对  -3 LEN 与实际长度不符  -4 CRC 校验失败
 * payload 指针指向 frame 内部，调用方不得在 frame 失效后使用。
 */
int sg_ble_parse(const uint8_t *frame, size_t len,
                 uint8_t *type, const uint8_t **payload);

/* 组装环境数据帧 */
size_t sg_ble_build_env(const sg_env_t *env, const sg_act_t *act,
                        uint8_t *out, size_t out_max);

/* 组装心跳帧 */
size_t sg_ble_build_heartbeat(uint8_t *out, size_t out_max);

/* 解析控制指令 payload */
int sg_ble_parse_cmd(const uint8_t *payload, uint8_t plen, sg_cmd_t *cmd);

/*
 * 字节流组帧器。
 *
 * BLE 模块透传过来的是一条字节流，帧与帧之间不保证对齐：可能一次收到半帧、
 * 也可能一次收到两帧半。所以不能假设"读一次就是完整一帧"，需要一个
 * 逐字节的组帧器：先找 SOF，按 LEN 等齐整帧，再校验 CRC；
 * 校验失败就从下一个字节重新找 SOF。
 */
typedef struct {
    uint8_t buf[SG_BLE_FRAME_MAX];
    size_t  len;
} sg_ble_rx_t;

void sg_ble_rx_reset(sg_ble_rx_t *rx);

/*
 * 喂入一个字节。凑满一帧且 CRC 通过时返回帧总长，并把类型与 payload
 * 长度写入 type / plen；否则返回 0。payload 内容仍在 rx->buf 内部，
 * 通过 sg_ble_rx_payload(rx) 取。
 */
int sg_ble_rx_push(sg_ble_rx_t *rx, uint8_t byte, uint8_t *type, uint8_t *plen);

const uint8_t *sg_ble_rx_payload(const sg_ble_rx_t *rx);

#endif /* SG_BLE_FRAME_H */
