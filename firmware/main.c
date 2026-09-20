#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "sensors.h"
#include "actuators.h"
#include "nvstore.h"
#include "control.h"
#include "ble_frame.h"
#include "display.h"

/*
 * 四个任务：传感器采集 / 执行器控制 / BLE 通信 / 系统监控。
 *
 * 数据流：
 *   采集任务 --q_sensor(长度1,Overwrite)--> BLE 任务上报 + 控制任务取最新值
 *   BLE 任务 --q_cmd--> 控制任务（手动模式下优先于自动闭环）
 *
 * 共享资源是**状态屏所在的 I2C 总线**：屏上一共 8 页，
 *   页 1 与页 7（模式 / 链路 / 运行时间）由监控任务刷新，
 *   页 2-6（环境数值与进度条）由采集任务刷新。
 * 两个任务页区间不重叠，但用的是同一块显存和同一条总线 ——
 * 如果两边同时进来，"设页地址 -> 写 128 字节显存"这两步会被交错到总线上，
 * 屏上就会出现串页乱码。所以整段"改显存 + 推屏"用互斥量圈起来。
 */

/* ---------- 任务句柄与通信对象 ---------- */
static QueueHandle_t     q_sensor;     /* 采集任务 -> BLE / 控制任务 */
static QueueHandle_t     q_cmd;        /* BLE 任务 -> 控制任务 */
static SemaphoreHandle_t mtx_i2c;      /* 保护状态屏显存与 I2C 总线 */

static sg_params_t      g_params;
static volatile uint8_t g_mode = SG_MODE_AUTO;
static uint32_t         g_boot_ms;     /* 开机时刻，用于算运行时间 */

static volatile uint32_t alive_sensor, alive_ctrl, alive_ble;

/* 心跳：每 1s 发一帧，连续 3 次没收到 App 应答就认为链路已断 */
#define HB_PERIOD_MS   1000u
#define HB_MISS_LIMIT  3u
static volatile uint32_t g_hb_miss;

/*
 * 采集周期的下限。
 * sample_period 是从 Flash 里读出来的参数，App 可以改，也可能是脏数据
 * （CRC 能挡住大部分，挡不住"值本身合法却离谱"的情况）。
 * 万一拿到 0，vTaskDelay(0) 只是让出一次 CPU，这个优先级最高的任务会立刻
 * 又跑起来，把控制、BLE、监控三个任务全部饿死 —— 而且因为每个任务的
 * 存活计数都还在涨，监控任务的卡死检测也发现不了。
 */
#define SAMPLE_PERIOD_MIN_MS  200u

/* ---------- 传感器采集任务 ---------- */
static void task_sensor(void *arg)
{
    sg_env_t env = {0};
    sg_act_t act = {0, 0};
    uint32_t period;
    (void)arg;

    sensors_prime();

    for (;;) {
        /* 传感器只有本任务访问，DHT22 的单总线时序不需要加锁 */
        sensors_sample(&env);

        /* 队列长度 1 + Overwrite：BLE 任务来不及取就走最新值，
           不让采集任务被慢消费者拖住（采集周期必须稳定） */
        xQueueOverwrite(q_sensor, &env);

        /* 刷面板：显存与 I2C 总线是和监控任务共用的，必须串起来。
           拿不到锁就跳过这一屏，绝不能因此把采集周期拖长 */
        act.pump  = actuators_get_duty(SG_PWM_PUMP);
        act.light = actuators_get_duty(SG_PWM_LIGHT);
        if (xSemaphoreTake(mtx_i2c, pdMS_TO_TICKS(50)) == pdTRUE) {
            display_update_env(&env, &act);
            xSemaphoreGive(mtx_i2c);
        }

        alive_sensor++;

        period = g_params.sample_period;
        if (period < SAMPLE_PERIOD_MIN_MS) period = SAMPLE_PERIOD_MIN_MS;
        vTaskDelay(pdMS_TO_TICKS(period));
    }
}

/* ---------- 执行器控制任务 ---------- */
static void task_ctrl(void *arg)
{
    sg_env_t env = {0};
    sg_cmd_t cmd;
    sg_act_t act;
    /*
     * 手动模式下要**持续维持**的占空比。
     *
     * 为什么必须单独存一份：App 下发的手动指令是"边沿"事件（进队列一次就
     * 没了），而 PWM 输出是"电平"—— 指令被消费掉之后每一轮循环都得重新
     * 写一遍寄存器。早期版本只在收到指令的那一瞬间写了一次，200ms 之后
     * 循环继续往下走，立刻被下面的自动闭环覆盖掉：面板上显示 MANUAL，
     * 实际水泵/补光灯仍按土壤和光照阈值在动，手动模式形同虚设。
     * 调试时就是这么暴露出来的：下发 pump=75%，读回 TIM3_CCR1
     * 仍然是自动闭环算出来的 299 而不是 749。
     */
    uint16_t manual_pump  = 0;
    uint16_t manual_light = 0;

    (void)arg;

    for (;;) {
        if (xQueueReceive(q_cmd, &cmd, 0) == pdTRUE) {
            g_mode = cmd.mode;
            if (cmd.mode == SG_MODE_MANUAL) {
                /* 上限钳位在 sg_control_apply_manual 里做，这里只负责记住结果 */
                sg_control_apply_manual(&cmd, &act);
                manual_pump  = act.pump;
                manual_light = act.light;
            }
            /* mode == AUTO：把这次指令当成"切回自动"，继续往下走闭环 */
        }

        if (g_mode == SG_MODE_MANUAL) {
            /* 手动模式优先于自动闭环：每轮都把锁存的占空比重新写下去 */
            act.pump  = manual_pump;
            act.light = manual_light;
            actuators_apply(&act);
            alive_ctrl++;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* 自动模式：按最新的环境数据做阈值闭环 */
        if (xQueuePeek(q_sensor, &env, 0) == pdTRUE) {
            sg_control_update(&env, &g_params, &act);
            actuators_apply(&act);
        }
        alive_ctrl++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------- BLE 通信任务 ---------- */
static void task_ble(void *arg)
{
    sg_ble_rx_t rx;
    sg_env_t    env;
    sg_act_t    act = {0, 0};
    uint8_t     type, plen;
    uint8_t     buf[SG_BLE_FRAME_MAX];
    uint8_t     frame[SG_BLE_FRAME_MAX];
    uint32_t    now, last_hb;
    int         i, n;

    (void)arg;
    sg_ble_rx_reset(&rx);
    now = last_hb = board_millis();

    for (;;) {
        /* 1. 收：从 UART 取字节，交给组帧器切帧 */
        n = board_ble_recv(buf, sizeof(buf));
        for (i = 0; i < n; i++) {
            if (sg_ble_rx_push(&rx, buf[i], &type, &plen) <= 0) continue;

            if (type == SG_BLE_T_CMD) {
                sg_cmd_t cmd;
                if (sg_ble_parse_cmd(sg_ble_rx_payload(&rx), plen, &cmd) == 0) {
                    xQueueSend(q_cmd, &cmd, 0);      /* 队列满就丢，控制任务仍在跑 */
                }
            }
            if (type == SG_BLE_T_HB || type == SG_BLE_T_CMD) {
                g_hb_miss = 0;                       /* App 有动静就算活着 */
            }
        }

        /* 2. 发：有新环境数据就上报一帧 */
        if (xQueuePeek(q_sensor, &env, 0) == pdTRUE) {
            act.pump  = actuators_get_duty(SG_PWM_PUMP);
            act.light = actuators_get_duty(SG_PWM_LIGHT);
            {
                size_t len = sg_ble_build_env(&env, &act, frame, sizeof(frame));
                if (len) board_ble_send(frame, (uint16_t)len);
            }
        }

        /* 3. 心跳保活 */
        now = board_millis();
        if ((now - last_hb) >= HB_PERIOD_MS) {
            size_t len;

            last_hb = now;
            len = sg_ble_build_heartbeat(frame, sizeof(frame));
            if (len) board_ble_send(frame, (uint16_t)len);

            if (g_hb_miss < 0xFFu) g_hb_miss++;
            if (g_hb_miss >= HB_MISS_LIMIT) {
                /*
                 * 连续 3 次没收到应答 -> 视为链路已断。
                 * 这时必须强制关断执行器并回到自动模式：
                 * 断线时若还停在手动模式的占空比上，水泵可能一直转。
                 */
                actuators_all_off();
                g_mode = SG_MODE_AUTO;
            }
        }

        alive_ble++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ---------- 系统监控任务 ---------- */

/*
 * 卡死判据：某个任务的存活计数**连续 N 次**没变，才认为它卡死。
 *
 * N 必须让判据的时间窗口大于"最慢任务的周期"。采集任务的周期是
 * sample_period（默认 2000ms —— DHT22 要求两次读取间隔不小于 2 秒，
 * 见 nvstore.c），而本任务每 1000ms 检查一次：如果只看"这一次和上一次
 * 比变没变"，采集任务在相邻两次检查之间本来就来不及变，健康运行时也会
 * 被判成卡死，随即进入下面"故意不喂狗"的死循环，4 秒后被 IWDG 复位整机。
 *
 * 这个缺陷编译和静态分析都看不出问题，是整机跑起来才暴露的：
 * 稳定地在启动后第 4 秒复位一次，现象是"固件只能跑一轮"。
 *
 * 取 4 次 = 4 秒窗口 > 2 秒的采集周期，留了一倍余量：采集计数每 2 秒
 * 必变一次，所以健康运行时最多只会连续 2 次没变。
 */
#define MONITOR_STUCK_LIMIT  4u

static void task_monitor(void *arg)
{
    static uint32_t last[3] = {0, 0, 0};
    static uint32_t miss[3] = {0, 0, 0};
    static int      led;
    (void)arg;

    for (;;) {
        uint32_t cur[3] = {alive_sensor, alive_ctrl, alive_ble};
        int i;

        for (i = 0; i < 3; i++) {
            if (cur[i] == last[i]) {
                /*
                 * 这一次没变。单次没变是正常的（周期比本任务长的任务必然
                 * 如此），所以先累计，连续 MONITOR_STUCK_LIMIT 次才判定卡死。
                 */
                if (++miss[i] >= MONITOR_STUCK_LIMIT) {
                    /*
                     * 确认真卡死。这里**故意不再喂狗**，让 IWDG 到点复位整机。
                     * 不在这里直接复位是因为此时系统状态已不可信，
                     * 让硬件看门狗兜底比在软件里"自救"更可靠。
                     */
                    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
                }
            } else {
                miss[i] = 0;
            }
            last[i] = cur[i];
        }

        /* 面板第二部分：模式、链路状态、运行时间。
           与采集任务抢同一条 I2C 总线，拿不到锁就直接跳过这一轮 */
        if (xSemaphoreTake(mtx_i2c, pdMS_TO_TICKS(100)) == pdTRUE) {
            display_update_status(g_mode, g_hb_miss,
                                  (board_millis() - g_boot_ms) / 1000u);
            xSemaphoreGive(mtx_i2c);
        }

        /*
         * 喂狗周期必须明显短于 IWDG 的超时，否则系统健康运行时也会被复位。
         * 这里 1 秒喂一次、IWDG 4 秒超时（board_init 里传的 4000），留了 4 倍余量。
         * 反过来也要注意：本地这份喂狗不能放在上面的 display 调用之前 ——
         * 那样"屏挂了导致刷新变慢"就永远触发不了复位。
         */
        board_watchdog_feed();

        /* 心跳灯。屏在调试期常常不插，这个灯是判断"固件还在跑"最直接的信号 */
        led = !led;
        board_led_set(led);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---------- 参数装载 ---------- */
static void params_load(void)
{
    uint8_t buf[64];
    sg_params_t loaded;

    if (board_flash_read(0, buf, sizeof(buf)) == 0
        && sg_params_deserialize(buf, sizeof(buf), &loaded)) {
        g_params = loaded;
        return;
    }

    /* CRC 不通过（首次上电 / 掉电写入不完整）-> 回落默认值并写回一次 */
    sg_params_default(&loaded);
    if (sg_params_serialize(&loaded, buf, sizeof(buf)) > 0) {
        board_flash_write(0, buf, sizeof(buf));
    }
    g_params = loaded;
}

int main(void)
{
    board_init();
    actuators_init();
    display_init();     /* 状态屏：发初始化指令 + 画标题，此时还没起调度器 */
    params_load();

    g_boot_ms = board_millis();

    q_sensor = xQueueCreate(1, sizeof(sg_env_t));
    q_cmd    = xQueueCreate(4, sizeof(sg_cmd_t));
    mtx_i2c  = xSemaphoreCreateMutex();

    xTaskCreate(task_sensor,  "sensor",  256, NULL, 3, NULL);
    xTaskCreate(task_ctrl,    "ctrl",    256, NULL, 2, NULL);
    xTaskCreate(task_ble,     "ble",     512, NULL, 2, NULL);
    xTaskCreate(task_monitor, "monitor", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    for (;;) { }
}
