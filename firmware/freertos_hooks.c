#include "stm32f4xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * FreeRTOS 与 HAL / 硬件之间的几处接线，以及内核钩子的落地实现。
 *
 * 单独放一个文件，是为了让 main.c 里只剩下"业务"：
 * 任务怎么划分、队列怎么流转，而不是一堆弱符号实现混在中间。
 */

/* ---------------- HAL 时基 ---------------- */

/*
 * 覆盖 HAL 的默认实现（stm32f4xx_hal.c 里那份是 __weak 的）。
 *
 * 默认实现会在 HAL_Init() 里去配 SysTick 并打开它的中断。但本工程
 * 把 SysTick 完全交给 FreeRTOS（见 FreeRTOSConfig.h 末尾的三个 #define），
 * 此时 SysTick_Handler 已经被改名成 xPortSysTickHandler：
 *   - 若仍让 HAL 先打开 SysTick，那些中断会在调度器启动前就打进来，
 *     而此刻 FreeRTOS 的就绪链表还没建好；
 *   - 而且 HAL_IncTick() 从此没人调用，HAL_Delay 会永远等下去。
 *
 * 所以这里干脆不配 SysTick，交由 FreeRTOS 在 xPortStartScheduler 里统一配置，
 * HAL 的计数改由下面的 tick hook 供货。两边用的是同一个 1kHz 计数，
 * 不会出现"FreeRTOS 时间"和"HAL 时间"各走各的情况。
 *
 * 一个可观察的后果：调度器启动之前 HAL_GetTick() 恒为 0，
 * 因此**启动阶段不能调 HAL_Delay / board_delay_ms**。
 * 本工程的启动路径（board_init / display_init / params_load）都满足这一条，
 * 需要延时的地方用的是 DWT 忙等 delay_us()。
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    return HAL_OK;
}

/*
 * FreeRTOS 每个 tick 会调一次这里（configUSE_TICK_HOOK = 1）。
 * 借它把 HAL 的毫秒计数推进一步，从而让 HAL_GetTick / HAL_Delay 可用。
 */
void vApplicationTickHook(void)
{
    HAL_IncTick();
}

/* ---------------- 内核钩子 ---------------- */

/*
 * 任务栈溢出。到这里说明某个任务的栈给小了，已经在踩别人的内存了。
 *
 * 这里不停机也不自己复位，而是关中断就地死等 —— 独立看门狗（IWDG）在
 * 4 秒内会把整机复位。之所以不直接 NVIC_SystemReset()：
 * 卡在原地可以接调试器看 pxCurrentTCB / 栈水位，复位掉就什么都看不到了；
 * 而看门狗兜底同样能保证"不会带着损坏的内存继续跑"。
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();
    for (;;) { }
}

/*
 * pvPortMalloc 返回了 NULL：堆不够。
 * 常见原因是 configTOTAL_HEAP_SIZE 给小了，或者哪里在反复分配不释放。
 * 缺了队列/互斥量的系统继续跑只会以更隐蔽的方式出错，同样交给看门狗复位。
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}

/*
 * configASSERT 的落地（见 FreeRTOSConfig.h）。
 * 触发点在这里停下，可以用调试器看 file/line 定位是哪条断言。
 * 变量加 volatile 且非 static，避免被优化掉。
 */
volatile const char *g_assert_file;
volatile int         g_assert_line;

void sg_assert_failed(const char *file, int line)
{
    g_assert_file = file;
    g_assert_line = line;

    taskDISABLE_INTERRUPTS();
    for (;;) { }
}
