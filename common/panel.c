#include "panel.h"

/*
 * 面板排版。所有绘制都走 ssd1306 的字符/图形原语。
 * 一个字符占 6 像素（5 列字形 + 1 列间距），128 列正好放 21 个字符。
 */

/* 链路判定阈值，与 main.c 里的心跳计数保持一致 */
#define PANEL_HB_MISS_LIMIT 3u

/* 1 个字符 6 像素。写成宏，省得各处散落魔法数字 */
#define CX(n)  ((n) * SSD1306_CHAR_W)

#define PG_T   (PANEL_ENV_FIRST + 0u)
#define PG_SOIL (PANEL_ENV_FIRST + 1u)
#define PG_DUTY (PANEL_ENV_FIRST + 2u)
#define PG_MOIST (PANEL_ENV_FIRST + 3u)
#define PG_LIGHT (PANEL_ENV_FIRST + 4u)

/* 进度条所在的像素行：每页 8 像素，正好一页一条 */
#define BAR_Y_MOIST 40
#define BAR_Y_LIGHT 48
#define BAR_X       CX(6)                          /* 左边留给 6 个字符的标签 */
#define BAR_W       (SSD1306_WIDTH - BAR_X)
#define BAR_H       8

static void set_temp_line(ssd1306_t *d, uint16_t temp_x10)
{
    /* "T 25.3C" 拆成定宽字段画，避免为了一行文本引入 printf */
    ssd1306_text(d, CX(0), PG_T, "T");
    ssd1306_u32 (d, CX(2), PG_T, temp_x10 / 10u, 2, ' ');
    ssd1306_text(d, CX(4), PG_T, ".");
    ssd1306_u32 (d, CX(5), PG_T, temp_x10 % 10u, 1, ' ');
    ssd1306_text(d, CX(6), PG_T, "C");
}

static void set_humi_line(ssd1306_t *d, uint16_t humi_x10)
{
    /*
     * "H 61.2%" 紧接着温度那一行画。
     * 整数部分留 3 格：DHT22 的湿度上限是 100.0%，两位会放不下。
     */
    ssd1306_text(d, CX(10), PG_T, "H");
    ssd1306_u32 (d, CX(12), PG_T, humi_x10 / 10u, 3, ' ');
    ssd1306_text(d, CX(15), PG_T, ".");
    ssd1306_u32 (d, CX(16), PG_T, humi_x10 % 10u, 1, ' ');
    ssd1306_text(d, CX(17), PG_T, "%");
}

static void set_soil_line(ssd1306_t *d, uint16_t soil_pct, uint16_t lux)
{
    ssd1306_text(d, CX(0),  PG_SOIL, "SOIL");
    ssd1306_u32 (d, CX(5),  PG_SOIL, soil_pct, 3, ' ');
    ssd1306_text(d, CX(8),  PG_SOIL, "%");

    ssd1306_text(d, CX(10), PG_SOIL, "LUX");
    ssd1306_u32 (d, CX(14), PG_SOIL, lux, 5, ' ');
}

static void set_duty_line(ssd1306_t *d, const sg_act_t *act)
{
    /* 占空比内部是千分比，显示成百分比更直观 */
    ssd1306_text(d, CX(0),  PG_DUTY, "PUMP");
    ssd1306_u32 (d, CX(5),  PG_DUTY, act->pump / 10u, 3, ' ');
    ssd1306_text(d, CX(8),  PG_DUTY, "%");

    ssd1306_text(d, CX(10), PG_DUTY, "LED");
    ssd1306_u32 (d, CX(14), PG_DUTY, act->light / 10u, 3, ' ');
    ssd1306_text(d, CX(17), PG_DUTY, "%");
}

void panel_render_title(ssd1306_t *d)
{
    if (!d) return;

    ssd1306_text (d, 0, PANEL_PAGE_TITLE, "SMARTGARDEN");
    ssd1306_hline(d, 0, 7, SSD1306_WIDTH, 1);   /* 标题行的下边框 */
}

void panel_render_env(ssd1306_t *d, const sg_env_t *env, const sg_act_t *act)
{
    uint16_t temp, humi;
    int      lux_permille, soil_permille;

    if (!d || !env || !act) return;

    temp = env->temp;
    humi = env->humi;

    /*
     * 再钳一次上下界。dht22_decode 已经把负温钳到 0，但校验和恰好撞对的
     * 异常帧仍可能给出超量程值；定宽字段被撑破后会串到右边字段上，
     * 所以这里按 DHT22 的量程（-40~80℃ / 0~100%）兜一次底。
     */
    if (temp > 800u)  temp = 800u;
    if (humi > 1000u) humi = 1000u;

    set_temp_line(d, temp);
    set_humi_line(d, humi);
    set_soil_line(d, env->soil, env->light);
    set_duty_line(d, act);

    /* 土壤湿度本身就是 0-100%，换算成千分比即可 */
    soil_permille = (env->soil > 100u) ? 1000 : (int)env->soil * 10;

    /* 光照没有天然满量程，按 PANEL_LUX_FULL_SCALE 折算 */
    if (env->light >= PANEL_LUX_FULL_SCALE) {
        lux_permille = 1000;
    } else {
        lux_permille = (int)((uint32_t)env->light * 1000u / PANEL_LUX_FULL_SCALE);
    }

    ssd1306_text(d, CX(0), PG_MOIST, "MOIST");
    ssd1306_bar (d, BAR_X, BAR_Y_MOIST, BAR_W, BAR_H, soil_permille);

    ssd1306_text(d, CX(0), PG_LIGHT, "LIGHT");
    ssd1306_bar (d, BAR_X, BAR_Y_LIGHT, BAR_W, BAR_H, lux_permille);
}

void panel_render_mode(ssd1306_t *d, uint8_t mode, uint32_t hb_miss)
{
    if (!d) return;

    /*
     * 先把模式名占的格子涂空再写。
     * ssd1306_text 只清除自己要画的那些格子，而 AUTO 是 4 个字符、
     * MANUAL 是 6 个字符 —— 从 MANUAL 切回 AUTO 时不做这一步，
     * 屏上会留下上一次的 "AL" 尾巴。
     */
    ssd1306_text(d, CX(0), PANEL_PAGE_MODE, "      ");
    ssd1306_text(d, CX(0), PANEL_PAGE_MODE, (mode == SG_MODE_MANUAL) ? "MANUAL" : "AUTO");

    /*
     * 链路状态放到第 9 列起：模式名最长 6 个字符（MANUAL），
     * 中间留两列空隙，读起来不会连成一片。
     * 判据和 main.c 的心跳一致（连续 N 次无应答视为断开）。
     */
    ssd1306_text(d, CX(9), PANEL_PAGE_MODE,
                 (hb_miss >= PANEL_HB_MISS_LIMIT) ? "LINK --" : "LINK OK");
}

void panel_render_uptime(ssd1306_t *d, uint32_t uptime_s)
{
    if (!d) return;

    ssd1306_text(d, CX(0),  PANEL_PAGE_UPTIME, "UP");
    ssd1306_u32 (d, CX(3),  PANEL_PAGE_UPTIME, uptime_s, 7, '0');
    ssd1306_text(d, CX(11), PANEL_PAGE_UPTIME, "S");
}
