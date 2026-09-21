#include "ssd1306.h"

#include <string.h>

/*
 * 5x7 字库，覆盖 ASCII 0x20 - 0x5F。
 *
 * 每个字形 7 行，每行只用低 5 位，bit4 对应最左一列。
 * 这里刻意用"行优先"存储（常见字库是列优先），因为行优先的数组在源码里
 * 从上往下读就是字形的样子，肉眼能直接核对，不容易出现某一笔写错位的情况。
 */
static const uint8_t FONT5X7[SSD1306_GLYPH_COUNT][7] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ' '  */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* '!'  */
    { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '"'  */
    { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A }, /* '#'  */
    { 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04 }, /* '$'  */
    { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 }, /* '%'  */
    { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D }, /* '&'  */
    { 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* '\'' */
    { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, /* '('  */
    { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, /* ')'  */
    { 0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00 }, /* '*'  */
    { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, /* '+'  */
    { 0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08 }, /* ','  */
    { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, /* '-'  */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 }, /* '.'  */
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }, /* '/'  */
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* '0'  */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* '1'  */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, /* '2'  */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, /* '3'  */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* '4'  */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /* '5'  */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* '6'  */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* '7'  */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* '8'  */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /* '9'  */
    { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 }, /* ':'  */
    { 0x00, 0x06, 0x06, 0x00, 0x06, 0x04, 0x08 }, /* ';'  */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* '<'  */
    { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }, /* '='  */
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, /* '>'  */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* '?'  */
    { 0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0F }, /* '@'  */
    { 0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11 }, /* 'A'  */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* 'B'  */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* 'C'  */
    { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C }, /* 'D'  */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* 'E'  */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* 'F'  */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, /* 'G'  */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* 'H'  */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 'I'  */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* 'J'  */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* 'K'  */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* 'L'  */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* 'M'  */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* 'N'  */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* 'O'  */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* 'P'  */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* 'Q'  */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* 'R'  */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* 'S'  */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* 'T'  */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* 'U'  */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* 'V'  */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, /* 'W'  */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* 'X'  */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* 'Y'  */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* 'Z'  */
    { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E }, /* '['  */
    { 0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01 }, /* '\\' */
    { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E }, /* ']'  */
    { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00 }, /* '^'  */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F }, /* '_'  */
};

/*
 * 上电初始化指令流。
 * 关键一条是 0x20 0x02：把寻址模式设成**页模式**（默认是水平模式）。
 * 页模式下每次写显存前要先用 0xB0|page 指定页、0x00/0x10 指定列，
 * 这与 ssd1306_flush_pages() 的分包方式一致；用水平模式的话写满一页会
 * 自动翻到下一页，就没法单独刷新某几页了。
 */
static const uint8_t INIT_CMDS[] = {
    0xAE,               /* 关显示（配置期间避免花屏） */
    0xD5, 0x80,         /* 时钟分频/振荡频率 */
    0xA8, 0x3F,         /* 多路复用比 = 64 */
    0xD3, 0x00,         /* 显示偏移 = 0 */
    0x40,               /* 显示起始行 = 0 */
    0x8D, 0x14,         /* 电荷泵开（模块内部升压到约 7.5V） */
    0x20, 0x02,         /* 寻址模式 = 页模式 */
    0xA1,               /* 段重映射（左右方向） */
    0xC8,               /* COM 扫描方向（上下方向） */
    0xDA, 0x12,         /* COM 引脚配置：交替 */
    0x81, 0xCF,         /* 对比度 */
    0xD9, 0xF1,         /* 预充电周期 */
    0xDB, 0x40,         /* VCOMH 去耦 */
    0xA4,               /* 显示内容跟随显存 */
    0xA6,               /* 正常显示（非反白） */
    0xAF                /* 开显示 */
};

/* ---------------- 显存操作 ---------------- */

void ssd1306_init(ssd1306_t *d)
{
    ssd1306_clear(d);
}

void ssd1306_clear(ssd1306_t *d)
{
    ssd1306_fill(d, 0x00u);
}

void ssd1306_fill(ssd1306_t *d, uint8_t pattern)
{
    if (!d) return;
    memset(d->fb, pattern, sizeof(d->fb));
}

void ssd1306_pixel(ssd1306_t *d, int x, int y, int on)
{
    uint8_t *p;

    if (!d) return;
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;

    p = &d->fb[(size_t)x + (size_t)(y / 8) * SSD1306_WIDTH];
    if (on) *p |=  (uint8_t)(1u << (y % 8));
    else    *p &= (uint8_t)~(1u << (y % 8));
}

int ssd1306_get_pixel(const ssd1306_t *d, int x, int y)
{
    if (!d) return 0;
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return 0;
    return (d->fb[(size_t)x + (size_t)(y / 8) * SSD1306_WIDTH] >> (y % 8)) & 1u;
}

void ssd1306_hline(ssd1306_t *d, int x, int y, int w, int on)
{
    int i;
    for (i = 0; i < w; i++) ssd1306_pixel(d, x + i, y, on);
}

void ssd1306_rect(ssd1306_t *d, int x, int y, int w, int h, int on)
{
    int i;

    if (w <= 0 || h <= 0) return;
    ssd1306_hline(d, x, y, w, on);
    ssd1306_hline(d, x, y + h - 1, w, on);
    for (i = 0; i < h; i++) {
        ssd1306_pixel(d, x, y + i, on);
        ssd1306_pixel(d, x + w - 1, y + i, on);
    }
}

void ssd1306_bar(ssd1306_t *d, int x, int y, int w, int h, int permille)
{
    int fill, i, j;

    if (w < 3 || h < 3) return;
    if (permille < 0)    permille = 0;
    if (permille > 1000) permille = 1000;

    ssd1306_rect(d, x, y, w, h, 1);        /* 外框 */

    /* 填充宽度按外框内部可用宽度算，避免最后一列压在右边框上 */
    fill = (w - 2) * permille / 1000;
    for (i = 0; i < fill; i++)
        for (j = 1; j < h - 1; j++)
            ssd1306_pixel(d, x + 1 + i, y + j, 1);
}

/* ---------------- 字符 ---------------- */

void ssd1306_char(ssd1306_t *d, int x, int page, char c)
{
    const uint8_t *g;
    int col, row;
    unsigned char uc = (unsigned char)c;

    if (!d) return;
    if (page < 0 || page >= SSD1306_PAGES) return;
    if (x <= -SSD1306_CHAR_W || x >= SSD1306_WIDTH) return;

    /* 先清掉这 6 列 x 7 行的旧内容，否则重绘时会留下残影 */
    for (col = 0; col < SSD1306_CHAR_W; col++)
        for (row = 0; row < SSD1306_CHAR_H; row++)
            ssd1306_pixel(d, x + col, page * 8 + row, 0);

    /* 字库只覆盖 0x20-0x5F；小写字母等留空（面板一律用大写） */
    if (uc < SSD1306_FONT_FIRST || uc >= SSD1306_FONT_FIRST + SSD1306_GLYPH_COUNT) return;

    g = FONT5X7[uc - SSD1306_FONT_FIRST];
    for (row = 0; row < SSD1306_CHAR_H; row++) {
        for (col = 0; col < 5; col++) {
            if (g[row] & (1u << (4 - col)))
                ssd1306_pixel(d, x + col, page * 8 + row, 1);
        }
    }
}

void ssd1306_text(ssd1306_t *d, int x, int page, const char *s)
{
    if (!d || !s) return;

    /*
     * 放不下整个字符就停：128 列宽 / 6 列每字符 = 21 个字符，
     * 多出来的 2 列宁可空着，也不要画半个字（半个字看起来像花屏）。
     */
    for (; *s; s++, x += SSD1306_CHAR_W) {
        if (x + SSD1306_CHAR_W > SSD1306_WIDTH) break;
        ssd1306_char(d, x, page, *s);
    }
}

void ssd1306_u32(ssd1306_t *d, int x, int page, uint32_t v, int width, char left_pad)
{
    char buf[12];
    int  n = 0, i;

    if (width < 1)  width = 1;
    if (width > 11) width = 11;

    /* 取各位数字，低位在前 */
    do {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v && n < width);

    /* 位数不够就在左边补 pad */
    for (i = n; i < width; i++) buf[i] = left_pad;

    /* buf 是低位在前，画的时候反过来：buf[width-1] 落在最左边 */
    for (i = 0; i < width; i++)
        ssd1306_char(d, x + (width - 1 - i) * SSD1306_CHAR_W, page, buf[i]);
}

const uint8_t *ssd1306_glyph(char c)
{
    unsigned char uc = (unsigned char)c;

    if (uc < SSD1306_FONT_FIRST || uc >= SSD1306_FONT_FIRST + SSD1306_GLYPH_COUNT) return NULL;
    return FONT5X7[uc - SSD1306_FONT_FIRST];
}

/* ---------------- 打包与推送 ---------------- */

size_t ssd1306_init_cmds(uint8_t *out, size_t out_max)
{
    size_t n = sizeof(INIT_CMDS);

    if (!out || out_max < n + 1u) return 0;
    out[0] = SSD1306_CTRL_CMD;
    memcpy(&out[1], INIT_CMDS, n);
    return n + 1u;
}

size_t ssd1306_set_page_cmds(uint8_t page, uint8_t *out, size_t out_max)
{
    if (!out || out_max < 4u) return 0;
    if (page >= SSD1306_PAGES) return 0;

    out[0] = SSD1306_CTRL_CMD;
    out[1] = (uint8_t)(0xB0u | page);   /* 页起始地址 */
    out[2] = 0x00u;                     /* 列地址低 4 位 = 0 */
    out[3] = 0x10u;                     /* 列地址高 4 位 = 0 */
    return 4u;
}

size_t ssd1306_page_payload(const ssd1306_t *d, uint8_t page, uint8_t *out, size_t out_max)
{
    if (!d || !out) return 0;
    if (out_max < SSD1306_WIDTH + 1u) return 0;
    if (page >= SSD1306_PAGES) return 0;

    out[0] = SSD1306_CTRL_DATA;
    memcpy(&out[1], &d->fb[(size_t)page * SSD1306_WIDTH], SSD1306_WIDTH);
    return SSD1306_WIDTH + 1u;
}

int ssd1306_flush_pages(const ssd1306_t *d, uint8_t first, uint8_t last,
                        ssd1306_write_fn wr, void *user)
{
    uint8_t cmd[4];
    uint8_t payload[SSD1306_WIDTH + 1u];
    uint8_t p;
    size_t  n;

    if (!d || !wr) return -1;
    if (first > last || last >= SSD1306_PAGES) return -1;

    for (p = first; p <= last; p++) {
        n = ssd1306_set_page_cmds(p, cmd, sizeof(cmd));
        if (!n) return -1;
        if (wr(user, cmd, (uint16_t)n) != 0) return -1;

        n = ssd1306_page_payload(d, p, payload, sizeof(payload));
        if (!n) return -1;
        if (wr(user, payload, (uint16_t)n) != 0) return -1;
    }
    return 0;
}

int ssd1306_flush(const ssd1306_t *d, ssd1306_write_fn wr, void *user)
{
    return ssd1306_flush_pages(d, 0, SSD1306_PAGES - 1u, wr, user);
}

int ssd1306_send_init(ssd1306_write_fn wr, void *user)
{
    uint8_t buf[1 + sizeof(INIT_CMDS)];
    size_t  n;

    if (!wr) return -1;
    n = ssd1306_init_cmds(buf, sizeof(buf));
    if (!n) return -1;
    return (wr(user, buf, (uint16_t)n) == 0) ? 0 : -1;
}
