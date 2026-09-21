#include "display.h"

#include "board.h"
#include "panel.h"
#include "ssd1306.h"

/*
 * 状态屏的板级封装：把"画到显存"（common/panel.c）和"发到屏上"
 * （common/ssd1306.c 打包 + board_i2c_write）接起来。
 *
 * 显存是 1KB，放不进任何任务的栈，所以做成模块内的静态变量。
 * 它与 I2C 总线都是采集任务和监控任务共用的资源，
 * 因此每次"改显存 + 推屏"都要在调用方持有的互斥量里一次做完。
 */

static ssd1306_t g_oled;
static uint8_t   g_present;   /* 屏是否在线（探测到应答后置 1） */

static int oled_write(void *user, const uint8_t *buf, uint16_t len)
{
    (void)user;
    return board_i2c_write(SSD1306_I2C_ADDR, buf, len);
}

static int push_pages(uint8_t first, uint8_t last)
{
    if (!g_present) return -1;

    if (ssd1306_flush_pages(&g_oled, first, last, oled_write, 0) != 0) {
        /*
         * 屏没插好 / 接线松了：置 0 之后就不再尝试，
         * 免得每帧都在超时上白等 200ms，把采集周期拖长。
         */
        g_present = 0;
        return -1;
    }
    return 0;
}

void display_init(void)
{
    ssd1306_init(&g_oled);
    panel_render_title(&g_oled);

    g_present = 0;
    if (ssd1306_send_init(oled_write, 0) != 0) return;   /* 无应答就到此为止 */
    g_present = 1;

    (void)push_pages(0, SSD1306_PAGES - 1u);
}

void display_update_env(const sg_env_t *env, const sg_act_t *act)
{
    panel_render_env(&g_oled, env, act);
    (void)push_pages(PANEL_ENV_FIRST, PANEL_ENV_LAST);
}

void display_update_status(uint8_t mode, uint32_t hb_miss, uint32_t uptime_s)
{
    panel_render_mode(&g_oled, mode, hb_miss);
    panel_render_uptime(&g_oled, uptime_s);

    /* 页 1 和页 7 分处两端，不是连续区间，分两次推 */
    (void)push_pages(PANEL_PAGE_MODE, PANEL_PAGE_MODE);
    (void)push_pages(PANEL_PAGE_UPTIME, PANEL_PAGE_UPTIME);
}
