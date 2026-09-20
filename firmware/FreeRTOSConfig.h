#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * FreeRTOS 配置：STM32F407 / Cortex-M4F / 168MHz / 1kHz tick。
 *
 * 这个文件不是从 CubeMX 生成的 —— 工程里每一条都有对应的理由，
 * 改动前请先确认下面注释里说的约束还成立。
 */

/* ---------------- 调度器 ---------------- */

#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0

/*
 * 168MHz 与 firmware/clock.c 里配的 SYSCLK 必须一致。
 * SysTick 直接取内核时钟，这个值错了 tick 就不是 1ms，
 * 所有 vTaskDelay / HAL_Delay 的时长会整体偏掉。
 * 两条时钟路径（HSE 8MHz、晶振失效退回 HSI 16MHz）都收敛到 168MHz，
 * 所以这里可以写死。
 */
#define configCPU_CLOCK_HZ                       ( 168000000UL )
#define configTICK_RATE_HZ                       ( 1000U )

/* main.c 里用到优先级 1/2/3，留到 7 即可（每级一个就绪链表，占 RAM） */
#define configMAX_PRIORITIES                     ( 7 )
#define configMINIMAL_STACK_SIZE                 ( 128 )
#define configMAX_TASK_NAME_LEN                  ( 8 )
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1

/* main.c 用 xSemaphoreCreateMutex() 串起状态屏显存与 I2C 总线 */
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configUSE_TASK_NOTIFICATIONS             1

#define configQUEUE_REGISTRY_SIZE                0
#define configUSE_QUEUE_SETS                     0

/* ---------------- 内存 ---------------- */

/*
 * heap_4（带相邻块合并）。只用动态分配，静态分配那套回调不写省事。
 *
 * 16KB 的账：4 个任务栈 256+256+512+256 字 = 5.5KB，
 * idle 栈 512B，5 个 TCB 约 0.5KB，队列/互斥量约 0.4KB，
 * 再留一倍余量给将来加任务。
 */
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 16 * 1024 ) )
#define configAPPLICATION_ALLOCATED_HEAP         0

/* ---------------- 钩子 ---------------- */

#define configUSE_IDLE_HOOK                      0

/*
 * tick hook 是本工程 HAL 时基的来源，见 firmware/freertos_hooks.c：
 * SysTick 整个交给 FreeRTOS，HAL_IncTick() 只能在这里补。
 */
#define configUSE_TICK_HOOK                      1

/* 栈溢出用方法 2：在栈尾填图案，切换时比对。比方法 1 慢一点，但能抓到更多情况 */
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configUSE_TIMERS                         0
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0

/* ---------------- 统计与调试 ---------------- */

#define configGENERATE_RUN_TIME_STATS            0
#define configUSE_TRACE_FACILITY                 0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0

/* ---------------- 中断优先级 ---------------- */

/* Cortex-M4 的 NVIC 只实现了高 4 位 */
#define configPRIO_BITS                          4

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5

/* 内核异常（PendSV/SysTick）走最低优先级 */
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* 优先级高于这个值的中断不允许调用 FreeRTOS 的 FromISR API */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ---------------- 按需裁剪的 API ---------------- */

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

/* ---------------- 断言 ---------------- */

extern void sg_assert_failed(const char *file, int line);

#define configASSERT( x )                                     \
    do {                                                      \
        if( ( x ) == 0 ) {                                    \
            sg_assert_failed( __FILE__, __LINE__ );           \
        }                                                     \
    } while( 0 )

/* ---------------- 内核异常挂到 CMSIS 向量名上 ---------------- */

/*
 * 这是 FreeRTOS 官方给 CMSIS 目标推荐的接法：
 * port.c 里的三个内核异常入口直接以向量表里的名字定义，
 * 不再需要一份手写的 stm32f4xx_it.c 去转发。
 *
 * 注意 SysTick 就此归 FreeRTOS 所有 —— stm32f4xx_hal.c 里那份
 * 负责 HAL_IncTick() 的 SysTick_Handler 不会生效（也没被链接进来），
 * HAL 时基改由 vApplicationTickHook() 提供。
 */
#define vPortSVCHandler                          SVC_Handler
#define xPortPendSVHandler                       PendSV_Handler
#define xPortSysTickHandler                      SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
