/*
 * BMS V1 专用 FreeRTOS configuration。`docs/FreeRTOS/FreeRTOSConfig.h` 是通用
 * reference input，不可复制替代本文件。
 * 冻结约束：configMAX_PRIORITIES=8；七任务 priority=5/4/3/3/2/2/2；
 * 4-bit NVIC、PriorityGroup_4、kernel raw 0xF0、max-syscall raw 0x50；
 * 12 KiB heap；configASSERT、stack overflow level 2 与 fatal hooks；
 * preemptive scheduler、1 ms tick、native FreeRTOS API。
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#include "bms_build_assert.h"

/* ------------------------------------------------------------------ */
/* Kernel 基础调度配置。 */
/* ------------------------------------------------------------------ */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (72000000UL)
#define configTICK_RATE_HZ                      (1000UL)
#define configMAX_PRIORITIES                    (8)
#define configMINIMAL_STACK_SIZE                (128)
#define configMAX_TASK_NAME_LEN                 (12)
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/* ------------------------------------------------------------------ */
/*
 * Memory（H-08）：七任务 stack/TCB、CAN/CC queue 与 RTOS object 共享 heap_4；
 * 12 KiB 已纳入 20 KiB SRAM linker boundary，不能在资源统计中重复相加。
 */
/* ------------------------------------------------------------------ */
#define configTOTAL_HEAP_SIZE                   (12 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP        0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

/* ------------------------------------------------------------------ */
/* Timer：V11.1.0 的 timers.c 要求 configUSE_TIMERS。 */
/* ------------------------------------------------------------------ */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (2)
#define configTIMER_QUEUE_LENGTH                (8)
#define configTIMER_TASK_STACK_DEPTH            (160)

/* ------------------------------------------------------------------ */
/* Fatal hooks 与 diagnostics（H-09）。 */
/* ------------------------------------------------------------------ */
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_APPLICATION_TASK_TAG          0

/* 正式代码使用的 V11 API 开关。 */
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_uxTaskGetStackHighWaterMark      1

/* ------------------------------------------------------------------ */
/* Cortex-M3 / ARMCC5 port 与 ISR priority contract（C-02）。 */
/* ------------------------------------------------------------------ */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

/* 将 FreeRTOS port exception handler 映射到 startup vector 使用的 RVDS 名称。 */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

/* ------------------------------------------------------------------ */
/* Assert（H-09）：application 提供 fail-stop handler。 */
/* ------------------------------------------------------------------ */
void vApplicationAssertFailedHandler(void);
#define configASSERT(x)                         \
    do                                         \
    {                                          \
        if ((x) == 0)                          \
        {                                      \
            taskDISABLE_INTERRUPTS();          \
            vApplicationAssertFailedHandler(); \
        }                                      \
    } while (0)

/* ------------------------------------------------------------------ */
/* hook implementation 位于 App/app_rtos_hooks.c；stack-overflow prototype 由 task.h 提供。 */
/* ------------------------------------------------------------------ */
void vApplicationAssertFailedHandler(void);
/* 在 FreeRTOS 堆分配失败时进入安全停机路径。 */
void vApplicationMallocFailedHook(void);
/* 提供 FreeRTOS idle 钩子，不转移任务安全职责。 */
void vApplicationIdleHook(void);

/* ------------------------------------------------------------------ */
/* 编译期一致性检查：配置漂移在 build 时 fail fast。 */
/* ------------------------------------------------------------------ */
BMS_BUILD_ASSERT(configMAX_PRIORITIES == 8,
                 freertos_max_priorities_is_eight);
BMS_BUILD_ASSERT(configTICK_RATE_HZ == 1000,
                 freertos_tick_is_one_khz);
BMS_BUILD_ASSERT(configPRIO_BITS == 4,
                 freertos_prio_bits_is_four);
BMS_BUILD_ASSERT(configKERNEL_INTERRUPT_PRIORITY == 0xF0,
                 kernel_raw_priority_is_f0);
BMS_BUILD_ASSERT(configMAX_SYSCALL_INTERRUPT_PRIORITY == 0x50,
                 max_syscall_raw_priority_is_50);
BMS_BUILD_ASSERT(configUSE_PORT_OPTIMISED_TASK_SELECTION == 1,
                 port_optimised_task_selection_enabled);
BMS_BUILD_ASSERT(configCHECK_FOR_STACK_OVERFLOW == 2,
                 stack_overflow_check_is_two);
BMS_BUILD_ASSERT(configTOTAL_HEAP_SIZE <= (20 * 1024),
                 heap_budget_inside_sram);

#endif /* FREERTOS_CONFIG_H：include guard */
