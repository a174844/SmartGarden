#ifndef SG_SSD1306_H
#define SG_SSD1306_H

#include <stdint.h>
#include <stddef.h>

/*
 * SSD1306 128x64 OLED（I2C 接口）的显存与指令封装。
 *
 * 这一层**不碰任何 I2C 硬件**，只做两件事：
 *   1. 维护 1KB 显存（128 列 x 8 页），并在上面画字符与图形；
 *   2. 把显存和指令打包成"一次 I2C 传输的载荷"（首字节是控制字节）。
 *
 * 真正的总线收发交给上层传入的回调，好处是驱动逻辑与总线完全解耦，
 * 换 MCU / 换 I2C 端口都不用动这部分。
 *
 * 注意 SSD1306 的显存是"页"结构：每页 8 个像素高、128 列宽，
 * 一列 8 个像素用一个字节表示，bit0 在最上面。所以字符按页定位最自然。
 */

#define SSD1306_WIDTH      128
#define SSD1306_HEIGHT     64
#define SSD1306_PAGES      (SSD1306_HEIGHT / 8)            /* 8 页 */
#define SSD1306_FB_SIZE    (SSD1306_WIDTH * SSD1306_PAGES) /* 1024 字节 */

/* 字库覆盖 ASCII 0x20 - 0x5F：空格、常用标点、数字、大写字母 */
#define SSD1306_FONT_FIRST  0x20
#define SSD1306_GLYPH_COUNT 64
#define SSD1306_CHAR_W      6   /* 5 列字形 + 1 列间距 */
#define SSD1306_CHAR_H      7
#define SSD1306_COLS        (SSD1306_WIDTH / SSD1306_CHAR_W)  /* 每行 21 个字符 */

/* 默认从机地址（SA0 接地）。变体模块可能用 0x3D */
#define SSD1306_I2C_ADDR    0x3Cu

/*
 * I2C 控制字节：一次传输的第一个字节决定后面跟的是什么。
 * 所有打包函数的 out[0] 都是这两个值之一。
 */
#define SSD1306_CTRL_CMD    0x00u   /* 后续字节是命令 */
#define SSD1306_CTRL_DATA   0x40u   /* 后续字节是显存数据 */

typedef struct {
    uint8_t fb[SSD1306_FB_SIZE];
} ssd1306_t;

/*
 * I2C 写回调：把 len 字节发到屏上。返回 0 表示成功。
 * 回调内部负责从机地址（固件里就是 board_i2c_write(SSD1306_I2C_ADDR, ...)）。
 */
typedef int (*ssd1306_write_fn)(void *user, const uint8_t *buf, uint16_t len);

void ssd1306_init(ssd1306_t *d);
void ssd1306_clear(ssd1306_t *d);
void ssd1306_fill(ssd1306_t *d, uint8_t pattern);

/* 画点与图元。坐标越界一律裁掉，不会写出显存之外 */
void ssd1306_pixel(ssd1306_t *d, int x, int y, int on);
int  ssd1306_get_pixel(const ssd1306_t *d, int x, int y);
void ssd1306_hline(ssd1306_t *d, int x, int y, int w, int on);
void ssd1306_rect (ssd1306_t *d, int x, int y, int w, int h, int on);
void ssd1306_bar  (ssd1306_t *d, int x, int y, int w, int h, int permille);

/* 字符按页定位：page 0..7 对应屏幕自上而下的 8 条 8 像素高的带 */
void ssd1306_char(ssd1306_t *d, int x, int page, char c);
void ssd1306_text(ssd1306_t *d, int x, int page, const char *s);

/* 十进制右对齐定宽输出，left_pad 为左侧填充字符（一般用空格或 '0'） */
void ssd1306_u32(ssd1306_t *d, int x, int page, uint32_t v, int width, char left_pad);

/*
 * 取字形点阵：返回 7 行、每行低 5 位（bit4 为最左列）。
 * 字库范围外的字符返回 NULL。
 * 暴露出来是为了能把显存直接反解回文字：排查排版问题时比对的是屏上
 * 真正显示的内容，而不是一堆像素坐标。
 */
const uint8_t *ssd1306_glyph(char c);

/*
 * 打包函数。out 的首字节是控制字节：
 *   0x00 = 后续字节都是指令，0x40 = 后续字节都是显存数据。
 * 返回写入 out 的字节数，超出 out_max 时返回 0。
 */
size_t ssd1306_init_cmds(uint8_t *out, size_t out_max);
size_t ssd1306_set_page_cmds(uint8_t page, uint8_t *out, size_t out_max);
size_t ssd1306_page_payload(const ssd1306_t *d, uint8_t page, uint8_t *out, size_t out_max);

/*
 * 推送到屏。按"先设页地址（4 字节指令）-> 再写该页 128 字节显存（129 字节）"
 * 分包，每页两次 I2C 传输。任何一次失败立即返回 -1，不再往下推。
 */
int ssd1306_flush_pages(const ssd1306_t *d, uint8_t first, uint8_t last,
                        ssd1306_write_fn wr, void *user);
int ssd1306_flush(const ssd1306_t *d, ssd1306_write_fn wr, void *user);

/* 只发初始化指令流（上电时调一次） */
int ssd1306_send_init(ssd1306_write_fn wr, void *user);

#endif /* SG_SSD1306_H */
